// AST -> LLVM IR -> object file.
//
// All values live in 32-byte mv_value slots and mutation goes through
// runtime calls; see DECISIONS.md.  The evalInto/evalPtr split is the seam
// where compiler type specialisation (ARCHITECTURE.md 3.3 option 3) will
// plug in: a provably-numeric variable's slot can become a bare i64 alloca
// behind these helpers without touching the parser.

#include "codegen.h"
#include "parser.h"     // CompileError

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"

#include <filesystem>
#include <functional>
#include <map>
#include <set>

using namespace llvm;

namespace mvx {

namespace {

const std::set<std::string> kIntrinsics = {
    "TIME", "SYSTEM", "INT", "SQRT", "ABS", "MOD",
};

// --------------------------------------------------------------------------
// Numeric specialisation analysis (ARCHITECTURE.md 3.3, option 3).
//
// A scalar or DIM'd array is "numeric" when every value stored into it is a
// provably numeric expression and it never escapes by reference (CALL
// argument, subroutine parameter).  Numeric scalars compile to a bare
// double alloca and numeric arrays to a flat double buffer; expressions
// over them lower to native FP instructions with no runtime calls.
// Demotion iterates to a fixed point because expression numericity depends
// on variable numericity.
//
// Two specialised tiers over the boxed representation:
//   Int — provably integral: bare i64 storage, native integer ops.
//         Deviation: i64 arithmetic wraps on overflow where boxed
//         arithmetic promotes to double; accepted and documented.
//   Dbl — provably numeric: bare double storage, native FP ops.  Boxed
//         arithmetic already promotes through double and compares
//         numerically via double, so this tier is exact to 2^53.
// Division always yields Dbl because MV division is fractional.
// Arrays additionally specialise to i8 storage when every value stored
// is an integer literal in 0..255 (flag arrays — the sieve's case).
enum class NK { Int, Dbl, NotNum };          // lattice: Int < Dbl < NotNum

inline NK joinNK(NK a, NK b) { return a > b ? a : b; }

class NumericAnalysis {
public:
    enum class ArrClass { Boxed, F64, I64, I8 };

    void run(const Program &prog) {
        for (const auto &p : prog.params) varK_[p] = NK::NotNum;
        collect(prog.body);
        bool changed = true;
        while (changed) {
            changed = false;
            scan(prog.body, changed);
        }
    }

    NK varKind(const std::string &n) const {
        if (arrays_.count(n)) return NK::NotNum;
        auto it = varK_.find(n);
        return it == varK_.end() ? NK::Int : it->second;
    }
    bool numericVar(const std::string &n) const {
        return varKind(n) != NK::NotNum;
    }

    ArrClass arrClass(const std::string &n) const {
        auto it = arrK_.find(n);
        NK k = it == arrK_.end() ? NK::Int : it->second;
        if (!arrays_.count(n) || k == NK::NotNum) return ArrClass::Boxed;
        if (k == NK::Dbl) return ArrClass::F64;
        return byteOnly_.count(n) ? ArrClass::I8 : ArrClass::I64;
    }
    bool numericArray(const std::string &n) const {
        return arrClass(n) != ArrClass::Boxed;
    }

    bool numericExpr(const Expr &e) const {
        return kindOf(e) != NK::NotNum;
    }

    NK kindOf(const Expr &e) const {
        switch (e.kind) {
        case Expr::K::IntLit: return NK::Int;
        case Expr::K::FltLit: return NK::Dbl;
        case Expr::K::StrLit: return NK::NotNum;
        case Expr::K::Var:    return varKind(e.sval);
        case Expr::K::Neg:    return kindOf(*e.lhs);
        case Expr::K::Not:
            return numericExpr(*e.lhs) ? NK::Int : NK::NotNum;
        case Expr::K::Paren: {
            if (arrays_.count(e.sval))
                switch (arrClass(e.sval)) {
                case ArrClass::I8:
                case ArrClass::I64: return NK::Int;
                case ArrClass::F64: return NK::Dbl;
                default:            return NK::NotNum;
                }
            if (!kIntrinsics.count(e.sval)) return NK::NotNum;
            for (const auto &a : e.args)
                if (!numericExpr(*a)) return NK::NotNum;
            const std::string &f = e.sval;
            if (f == "TIME" || f == "SYSTEM" || f == "INT") return NK::Int;
            if (f == "SQRT") return NK::Dbl;
            if (f == "ABS")  return kindOf(*e.args[0]);
            /* MOD */
            return joinNK(kindOf(*e.args[0]), kindOf(*e.args[1]));
        }
        case Expr::K::Bin: {
            if (e.op == BinOp::Cat) return NK::NotNum;
            NK l = kindOf(*e.lhs), r = kindOf(*e.rhs);
            if (l == NK::NotNum || r == NK::NotNum) return NK::NotNum;
            switch (e.op) {
            case BinOp::Add: case BinOp::Sub: case BinOp::Mul:
                return joinNK(l, r);
            case BinOp::Div: case BinOp::Pow:
                return NK::Dbl;
            default:                       // comparisons, AND, OR: 0 / 1
                return NK::Int;
            }
        }
        }
        return NK::NotNum;
    }

private:
    std::set<std::string> arrays_, byteUnsafe_, byteOnly_;
    std::map<std::string, NK> varK_, arrK_;

    static bool isByteLit(const Expr &e) {
        return e.kind == Expr::K::IntLit && e.ival >= 0 && e.ival <= 255;
    }

    void collect(const std::vector<StmtP> &stmts) {
        for (const auto &sp : stmts) {
            const Stmt &s = *sp;
            if (s.kind == Stmt::K::Dim) arrays_.insert(s.name);
            if (s.kind == Stmt::K::Call) {
                // By-reference arguments escape: the callee may store
                // anything into them.
                for (const auto &a : s.args) {
                    if (a->kind == Expr::K::Var)
                        varK_[a->sval] = NK::NotNum;
                    if (a->kind == Expr::K::Paren)
                        arrK_[a->sval] = NK::NotNum;
                }
            }
            if (s.kind == Stmt::K::Assign &&
                s.target->kind == Expr::K::Paren &&
                !isByteLit(*s.value))
                byteUnsafe_.insert(s.target->sval);
            collect(s.body); collect(s.elseBody);
            collect(s.pre);  collect(s.post);
        }
    }

    void joinVar(const std::string &n, NK k, bool &changed) {
        NK cur = varK_.count(n) ? varK_[n] : NK::Int;
        NK nw = joinNK(cur, k);
        if (nw != cur) { varK_[n] = nw; changed = true; }
    }
    void joinArr(const std::string &n, NK k, bool &changed) {
        NK cur = arrK_.count(n) ? arrK_[n] : NK::Int;
        NK nw = joinNK(cur, k);
        if (nw != cur) { arrK_[n] = nw; changed = true; }
    }

    void scan(const std::vector<StmtP> &stmts, bool &changed) {
        for (const auto &sp : stmts) {
            const Stmt &s = *sp;
            switch (s.kind) {
            case Stmt::K::Assign: {
                const Expr &t = *s.target;
                NK k = kindOf(*s.value);
                if (t.kind == Expr::K::Var) joinVar(t.sval, k, changed);
                if (t.kind == Expr::K::Paren && arrays_.count(t.sval))
                    joinArr(t.sval, k, changed);
                break;
            }
            case Stmt::K::For: {
                NK k = joinNK(kindOf(*s.from), kindOf(*s.to));
                if (s.step) k = joinNK(k, kindOf(*s.step));
                joinVar(s.name, k, changed);
                break;
            }
            default:
                break;
            }
            scan(s.body, changed); scan(s.elseBody, changed);
            scan(s.pre, changed);  scan(s.post, changed);
        }
        // byteOnly_ is derived, not part of the fixed point
        byteOnly_.clear();
        for (const auto &a : arrays_)
            if (!byteUnsafe_.count(a)) byteOnly_.insert(a);
    }
};

class CodeGen {
public:
    CodeGen(const Program &prog, const CodegenOptions &opts)
        : prog_(prog), opts_(opts),
          item_(std::filesystem::path(prog.sourcePath).filename().string()),
          mod_(item_, llctx_), b_(llctx_), eb_(llctx_), dib_(mod_) {}

    void run(const std::string &outPath);

private:
    const Program &prog_;
    const CodegenOptions &opts_;
    std::string item_;

    LLVMContext llctx_;
    Module mod_;
    IRBuilder<> b_;         // statement stream
    IRBuilder<> eb_;        // entry block: allocas + init calls
    DIBuilder dib_;

    Function *fn_ = nullptr;
    Value *ctxArg_ = nullptr;
    BasicBlock *retBB_ = nullptr;

    StructType *valTy_ = nullptr;
    PointerType *ptrTy_ = nullptr;
    Type *i64Ty_ = nullptr, *i32Ty_ = nullptr, *dblTy_ = nullptr,
         *voidTy_ = nullptr;

    DISubprogram *sp_ = nullptr;
    DIFile *diFile_ = nullptr;
    DICompositeType *diValTy_ = nullptr;

    std::map<std::string, Value *> scalars_;   // name -> ptr to mv_value
    std::map<std::string, Value *> arrays_;    // name -> alloca of mv_array*
    std::set<std::string> arrayNames_;         // every DIM'd name
    std::map<std::string, Constant *> strings_;

    NumericAnalysis num_;
    std::map<std::string, Value *> numVars_;   // name -> i64/double alloca
    struct NumArr { Value *ptr, *d1, *d2; Type *elemTy; };
    std::map<std::string, NumArr> numArrs_;

    std::vector<Value *> tempPool_;
    size_t tempUsed_ = 0;

    // ---------------------------------------------------------------- utils

    [[noreturn]] void err(int line, const std::string &msg) {
        throw CompileError(item_, line, msg);
    }

    FunctionCallee rt(const char *name, Type *ret, ArrayRef<Type *> params) {
        return mod_.getOrInsertFunction(
            name, FunctionType::get(ret, params, false));
    }

    Value *callRt(const char *name, Type *ret, ArrayRef<Type *> paramTys,
                  ArrayRef<Value *> args) {
        return b_.CreateCall(rt(name, ret, paramTys), args);
    }

    void call1(const char *name, Value *a) {
        callRt(name, voidTy_, {ptrTy_}, {a});
    }
    void call2(const char *name, Value *a, Value *b) {
        callRt(name, voidTy_, {ptrTy_, ptrTy_}, {a, b});
    }
    void call3(const char *name, Value *a, Value *b, Value *c) {
        callRt(name, voidTy_, {ptrTy_, ptrTy_, ptrTy_}, {a, b, c});
    }

    Constant *stringConst(const std::string &s) {
        auto it = strings_.find(s);
        if (it != strings_.end()) return it->second;
        Constant *g = b_.CreateGlobalString(s, ".str");
        strings_[s] = g;
        return g;
    }

    // Allocate one mv_value slot in the entry block, initialised once.
    Value *newSlot(const std::string &dbgName = "") {
        Value *slot = eb_.CreateAlloca(valTy_, nullptr, dbgName);
        eb_.CreateCall(rt("mv_init", voidTy_, {ptrTy_}), {slot});
        return slot;
    }

    Value *acquireTemp() {
        if (tempUsed_ == tempPool_.size())
            tempPool_.push_back(newSlot());
        return tempPool_[tempUsed_++];
    }

    Value *getScalar(const std::string &name, int line) {
        if (arrayNames_.count(name))
            err(line, "array " + name + " used without subscripts");
        auto it = scalars_.find(name);
        if (it != scalars_.end()) return it->second;
        Value *slot = newSlot(name);
        scalars_[name] = slot;
        declareVarDebug(name, slot, line);
        return slot;
    }

    Value *getArraySlot(const std::string &name) {
        auto it = arrays_.find(name);
        if (it != arrays_.end()) return it->second;
        Value *slot = eb_.CreateAlloca(ptrTy_, nullptr, name + ".arr");
        eb_.CreateStore(ConstantPointerNull::get(ptrTy_), slot);
        arrays_[name] = slot;
        return slot;
    }

    DebugLoc loc(int line) {
        return DILocation::get(llctx_, line, 1, sp_);
    }

    void declareVarDebug(const std::string &name, Value *slot, int line) {
        if (!sp_) return;
        DILocalVariable *dv = dib_.createAutoVariable(
            sp_, name, diFile_, (unsigned)line, diValTy_);
        dib_.insertDeclare(slot, dv, dib_.createExpression(),
                           DILocation::get(llctx_, (unsigned)line, 1, sp_),
                           b_.GetInsertBlock());
    }

    // ------------------------------------------------- numeric fast path

    bool intVar(const std::string &n) const {
        return num_.varKind(n) == NK::Int;
    }

    Value *numVarSlot(const std::string &name) {
        auto it = numVars_.find(name);
        if (it != numVars_.end()) return it->second;
        Type *ty = intVar(name) ? i64Ty_ : dblTy_;
        Value *slot = eb_.CreateAlloca(ty, nullptr, name);
        eb_.CreateStore(Constant::getNullValue(ty), slot);
        numVars_[name] = slot;
        return slot;
    }

    Type *arrElemTy(const std::string &name) {
        switch (num_.arrClass(name)) {
        case NumericAnalysis::ArrClass::I8:  return b_.getInt8Ty();
        case NumericAnalysis::ArrClass::I64: return i64Ty_;
        default:                             return dblTy_;
        }
    }

    NumArr &numArrSlots(const std::string &name) {
        auto it = numArrs_.find(name);
        if (it != numArrs_.end()) return it->second;
        NumArr a;
        a.ptr = eb_.CreateAlloca(ptrTy_, nullptr, name + ".nptr");
        a.d1  = eb_.CreateAlloca(i64Ty_, nullptr, name + ".d1");
        a.d2  = eb_.CreateAlloca(i64Ty_, nullptr, name + ".d2");
        a.elemTy = arrElemTy(name);
        eb_.CreateStore(ConstantPointerNull::get(ptrTy_), a.ptr);
        eb_.CreateStore(ConstantInt::get(i64Ty_, 0), a.d1);
        eb_.CreateStore(ConstantInt::get(i64Ty_, 0), a.d2);
        return numArrs_[name] = a;
    }

    // Saturating double -> i64; plain fptosi is UB out of range.
    Value *dblToI64(Value *v) {
        Function *f = Intrinsic::getOrInsertDeclaration(
            &mod_, Intrinsic::fptosi_sat, {i64Ty_, dblTy_});
        return b_.CreateCall(f, {v});
    }
    Value *asI64(const Expr &e) {
        Value *v = evalNum(e);
        return num_.kindOf(e) == NK::Int ? v : dblToI64(v);
    }
    Value *asDbl(const Expr &e) {
        Value *v = evalNum(e);
        return num_.kindOf(e) == NK::Int ? b_.CreateSIToFP(v, dblTy_) : v;
    }

    // i64 subscript from any expression.
    Value *numIndex(const Expr &e) {
        if (e.kind == Expr::K::IntLit)
            return ConstantInt::get(i64Ty_, e.ival);
        if (num_.numericExpr(e))
            return asI64(e);
        return callRt("mv_get_int", i64Ty_, {ptrTy_}, {evalPtr(e)});
    }

    // Bounds-checked pointer to a numeric array element.
    Value *numElemPtr(const Expr &e) {
        if (e.args.empty() || e.args.size() > 2)
            err(e.line, "array " + e.sval + " takes 1 or 2 subscripts");
        NumArr &a = numArrSlots(e.sval);
        Value *d1 = b_.CreateLoad(i64Ty_, a.d1);
        Value *d2 = b_.CreateLoad(i64Ty_, a.d2);
        Value *i = numIndex(*e.args[0]);
        Value *zero = ConstantInt::get(i64Ty_, 0);
        Value *one = ConstantInt::get(i64Ty_, 1);
        Value *j = e.args.size() == 2 ? numIndex(*e.args[1]) : zero;

        Value *bad;
        Value *idx;
        if (e.args.size() == 1) {
            bad = b_.CreateOr(b_.CreateICmpNE(d2, zero),
                              b_.CreateOr(b_.CreateICmpSLT(i, one),
                                          b_.CreateICmpSGT(i, d1)));
            idx = b_.CreateSub(i, one);
        } else {
            Value *badI = b_.CreateOr(b_.CreateICmpSLT(i, one),
                                      b_.CreateICmpSGT(i, d1));
            Value *badJ = b_.CreateOr(b_.CreateICmpSLT(j, one),
                                      b_.CreateICmpSGT(j, d2));
            bad = b_.CreateOr(b_.CreateICmpEQ(d2, zero),
                              b_.CreateOr(badI, badJ));
            idx = b_.CreateAdd(
                b_.CreateMul(b_.CreateSub(i, one), d2),
                b_.CreateSub(j, one));
        }
        BasicBlock *failBB = newBB("idx.fail");
        BasicBlock *okBB = newBB("idx.ok");
        b_.CreateCondBr(bad, failBB, okBB,
                        MDBuilder(llctx_).createUnlikelyBranchWeights());
        b_.SetInsertPoint(failBB);
        callRt("mvx_narr_fail", voidTy_, {i64Ty_, i64Ty_, i64Ty_, i64Ty_},
               {i, j, d1, d2});
        b_.CreateUnreachable();
        b_.SetInsertPoint(okBB);
        Value *base = b_.CreateLoad(ptrTy_, a.ptr);
        return b_.CreateGEP(a.elemTy, base, idx);
    }

    Value *fpIntrinsic(Intrinsic::ID id, ArrayRef<Value *> args) {
        Function *f =
            Intrinsic::getOrInsertDeclaration(&mod_, id, {dblTy_});
        return b_.CreateCall(f, args);
    }

    // Native value of a provably numeric expression: i64 when
    // num_.kindOf(e) == NK::Int, double when NK::Dbl.
    Value *evalNum(const Expr &e) {
        switch (e.kind) {
        case Expr::K::IntLit:
            return ConstantInt::get(i64Ty_, e.ival);
        case Expr::K::FltLit:
            return ConstantFP::get(dblTy_, e.fval);
        case Expr::K::Var: {
            Value *slot = numVarSlot(e.sval);
            return b_.CreateLoad(intVar(e.sval) ? i64Ty_ : dblTy_, slot);
        }
        case Expr::K::Paren: {
            if (arrayNames_.count(e.sval)) {
                Value *v = b_.CreateLoad(arrElemTy(e.sval), numElemPtr(e));
                if (v->getType() == b_.getInt8Ty())
                    v = b_.CreateZExt(v, i64Ty_);
                return v;
            }
            const std::string &f = e.sval;
            if (f == "TIME")
                return dblToI64(callRt("mvx_num_time", dblTy_, {}, {}));
            if (f == "SYSTEM")
                return dblToI64(callRt("mvx_num_system", dblTy_, {dblTy_},
                                       {asDbl(*e.args[0])}));
            if (f == "INT")
                return asI64(*e.args[0]);   // fptosi_sat truncates to zero
            if (f == "SQRT")
                return fpIntrinsic(Intrinsic::sqrt, {asDbl(*e.args[0])});
            if (f == "ABS") {
                const Expr &a = *e.args[0];
                if (num_.kindOf(a) == NK::Int) {
                    Function *fn = Intrinsic::getOrInsertDeclaration(
                        &mod_, Intrinsic::abs, {i64Ty_});
                    return b_.CreateCall(fn, {evalNum(a), b_.getFalse()});
                }
                return fpIntrinsic(Intrinsic::fabs, {evalNum(a)});
            }
            if (f == "MOD") {
                if (num_.kindOf(e) == NK::Int)
                    return callRt("mvx_num_imod", i64Ty_, {i64Ty_, i64Ty_},
                                  {evalNum(*e.args[0]), evalNum(*e.args[1])});
                return callRt("mvx_num_mod", dblTy_, {dblTy_, dblTy_},
                              {asDbl(*e.args[0]), asDbl(*e.args[1])});
            }
            err(e.line, f + " is not an intrinsic function or DIM'd array");
        }
        case Expr::K::Neg: {
            Value *v = evalNum(*e.lhs);
            return num_.kindOf(*e.lhs) == NK::Int ? b_.CreateNeg(v)
                                                  : b_.CreateFNeg(v);
        }
        case Expr::K::Not:
            return b_.CreateZExt(b_.CreateNot(evalCond(*e.lhs)), i64Ty_);
        case Expr::K::Bin: {
            bool bothInt = num_.kindOf(*e.lhs) == NK::Int &&
                           num_.kindOf(*e.rhs) == NK::Int;
            switch (e.op) {
            case BinOp::Add:
                return bothInt
                    ? b_.CreateAdd(evalNum(*e.lhs), evalNum(*e.rhs))
                    : b_.CreateFAdd(asDbl(*e.lhs), asDbl(*e.rhs));
            case BinOp::Sub:
                return bothInt
                    ? b_.CreateSub(evalNum(*e.lhs), evalNum(*e.rhs))
                    : b_.CreateFSub(asDbl(*e.lhs), asDbl(*e.rhs));
            case BinOp::Mul:
                return bothInt
                    ? b_.CreateMul(evalNum(*e.lhs), evalNum(*e.rhs))
                    : b_.CreateFMul(asDbl(*e.lhs), asDbl(*e.rhs));
            case BinOp::Div:
                return b_.CreateFDiv(asDbl(*e.lhs), asDbl(*e.rhs));
            case BinOp::Pow: {
                Function *f = Intrinsic::getOrInsertDeclaration(
                    &mod_, Intrinsic::pow, {dblTy_});
                return b_.CreateCall(f, {asDbl(*e.lhs), asDbl(*e.rhs)});
            }
            default:            // comparison / AND / OR as 0-1 value
                return b_.CreateZExt(evalCond(e), i64Ty_);
            }
        }
        case Expr::K::StrLit:
            break;
        }
        err(e.line, "internal error: evalNum on non-numeric expression");
    }

    // ----------------------------------------------------------- expressions

    Value *arrayElemPtr(const Expr &e) {
        Value *arr = b_.CreateLoad(ptrTy_, getArraySlot(e.sval));
        if (e.args.empty() || e.args.size() > 2)
            err(e.line, "array " + e.sval + " takes 1 or 2 subscripts");
        Value *i = numIndex(*e.args[0]);
        Value *j = e.args.size() == 2 ? numIndex(*e.args[1])
                                      : ConstantInt::get(i64Ty_, 0);
        return callRt("mv_arr_elem", ptrTy_, {ptrTy_, i64Ty_, i64Ty_},
                      {arr, i, j});
    }

    // Pointer to an mv_value holding the expression's value.  Lvalues are
    // returned in place (no copy); other expressions land in a temp.
    // Numeric expressions are boxed into a temp here — the bridge from the
    // fast path into string-land.
    // Box a numeric expression into the given slot, preserving intness.
    void boxNum(const Expr &e, Value *dest) {
        if (num_.kindOf(e) == NK::Int)
            callRt("mv_set_int", voidTy_, {ptrTy_, i64Ty_},
                   {dest, evalNum(e)});
        else
            callRt("mv_set_dbl", voidTy_, {ptrTy_, dblTy_},
                   {dest, evalNum(e)});
    }

    Value *evalPtr(const Expr &e) {
        if (num_.numericExpr(e)) {
            Value *t = acquireTemp();
            boxNum(e, t);
            return t;
        }
        if (e.kind == Expr::K::Var)
            return getScalar(e.sval, e.line);
        if (e.kind == Expr::K::Paren && arrayNames_.count(e.sval))
            return arrayElemPtr(e);
        Value *t = acquireTemp();
        evalInto(e, t);
        return t;
    }

    void evalInto(const Expr &e, Value *dest) {
        if (num_.numericExpr(e)) {
            boxNum(e, dest);
            return;
        }
        switch (e.kind) {
        case Expr::K::IntLit:
            callRt("mv_set_int", voidTy_, {ptrTy_, i64Ty_},
                   {dest, ConstantInt::get(i64Ty_, e.ival)});
            return;
        case Expr::K::FltLit:
            callRt("mv_set_dbl", voidTy_, {ptrTy_, dblTy_},
                   {dest, ConstantFP::get(dblTy_, e.fval)});
            return;
        case Expr::K::StrLit:
            callRt("mv_set_str", voidTy_, {ptrTy_, ptrTy_, i64Ty_},
                   {dest, stringConst(e.sval),
                    ConstantInt::get(i64Ty_, (int64_t)e.sval.size())});
            return;
        case Expr::K::Var:
            call2("mv_copy", dest, getScalar(e.sval, e.line));
            return;
        case Expr::K::Paren:
            evalParenInto(e, dest);
            return;
        case Expr::K::Neg:
            call2("mv_neg", dest, evalPtr(*e.lhs));
            return;
        case Expr::K::Not: {
            Value *t = evalCond(*e.lhs);
            Value *inv = b_.CreateZExt(b_.CreateNot(t), i64Ty_);
            callRt("mv_set_int", voidTy_, {ptrTy_, i64Ty_}, {dest, inv});
            return;
        }
        case Expr::K::Bin:
            evalBinInto(e, dest);
            return;
        }
    }

    void evalParenInto(const Expr &e, Value *dest) {
        if (arrayNames_.count(e.sval)) {
            call2("mv_copy", dest, arrayElemPtr(e));
            return;
        }
        const std::string &f = e.sval;
        auto need = [&](size_t n) {
            if (e.args.size() != n)
                err(e.line, f + "() takes " + std::to_string(n) +
                                " argument(s)");
        };
        if (f == "TIME")   { need(0); call1("mv_time", dest); return; }
        if (f == "SYSTEM") { need(1);
            call2("mv_system_fn", dest, evalPtr(*e.args[0])); return; }
        if (f == "INT")    { need(1);
            call2("mv_int_fn", dest, evalPtr(*e.args[0])); return; }
        if (f == "SQRT")   { need(1);
            call2("mv_sqrt_fn", dest, evalPtr(*e.args[0])); return; }
        if (f == "ABS")    { need(1);
            call2("mv_abs_fn", dest, evalPtr(*e.args[0])); return; }
        if (f == "MOD")    { need(2);
            call3("mv_mod_fn", dest, evalPtr(*e.args[0]),
                  evalPtr(*e.args[1])); return; }
        err(e.line, f + " is not an intrinsic function or DIM'd array");
    }

    void evalBinInto(const Expr &e, Value *dest) {
        switch (e.op) {
        case BinOp::Add: case BinOp::Sub: case BinOp::Mul:
        case BinOp::Div: case BinOp::Pow: case BinOp::Cat: {
            static const std::map<BinOp, const char *> fns = {
                {BinOp::Add, "mv_add"}, {BinOp::Sub, "mv_sub"},
                {BinOp::Mul, "mv_mul"}, {BinOp::Div, "mv_div"},
                {BinOp::Pow, "mv_pow"}, {BinOp::Cat, "mv_cat"},
            };
            Value *pa = evalPtr(*e.lhs);
            Value *pb = evalPtr(*e.rhs);
            call3(fns.at(e.op), dest, pa, pb);
            return;
        }
        default: {
            Value *c = evalCond(e);
            callRt("mv_set_int", voidTy_, {ptrTy_, i64Ty_},
                   {dest, b_.CreateZExt(c, i64Ty_)});
            return;
        }
        }
    }

    // Boolean contexts skip boxing entirely.
    Value *evalCond(const Expr &e) {
        if (e.kind == Expr::K::Bin) {
            switch (e.op) {
            case BinOp::Eq: case BinOp::Ne: case BinOp::Lt:
            case BinOp::Le: case BinOp::Gt: case BinOp::Ge: {
                if (num_.numericExpr(*e.lhs) && num_.numericExpr(*e.rhs)) {
                    if (num_.kindOf(*e.lhs) == NK::Int &&
                        num_.kindOf(*e.rhs) == NK::Int) {
                        Value *l = evalNum(*e.lhs);
                        Value *r = evalNum(*e.rhs);
                        switch (e.op) {
                        case BinOp::Eq: return b_.CreateICmpEQ(l, r);
                        case BinOp::Ne: return b_.CreateICmpNE(l, r);
                        case BinOp::Lt: return b_.CreateICmpSLT(l, r);
                        case BinOp::Le: return b_.CreateICmpSLE(l, r);
                        case BinOp::Gt: return b_.CreateICmpSGT(l, r);
                        default:        return b_.CreateICmpSGE(l, r);
                        }
                    }
                    Value *l = asDbl(*e.lhs);
                    Value *r = asDbl(*e.rhs);
                    switch (e.op) {
                    case BinOp::Eq: return b_.CreateFCmpOEQ(l, r);
                    case BinOp::Ne: return b_.CreateFCmpONE(l, r);
                    case BinOp::Lt: return b_.CreateFCmpOLT(l, r);
                    case BinOp::Le: return b_.CreateFCmpOLE(l, r);
                    case BinOp::Gt: return b_.CreateFCmpOGT(l, r);
                    default:        return b_.CreateFCmpOGE(l, r);
                    }
                }
                Value *pa = evalPtr(*e.lhs);
                Value *pb = evalPtr(*e.rhs);
                Value *c = callRt("mv_compare", i64Ty_, {ptrTy_, ptrTy_},
                                  {pa, pb});
                Value *zero = ConstantInt::get(i64Ty_, 0);
                switch (e.op) {
                case BinOp::Eq: return b_.CreateICmpEQ(c, zero);
                case BinOp::Ne: return b_.CreateICmpNE(c, zero);
                case BinOp::Lt: return b_.CreateICmpSLT(c, zero);
                case BinOp::Le: return b_.CreateICmpSLE(c, zero);
                case BinOp::Gt: return b_.CreateICmpSGT(c, zero);
                default:        return b_.CreateICmpSGE(c, zero);
                }
            }
            case BinOp::And:
                return b_.CreateAnd(evalCond(*e.lhs), evalCond(*e.rhs));
            case BinOp::Or:
                return b_.CreateOr(evalCond(*e.lhs), evalCond(*e.rhs));
            default:
                break;
            }
        }
        if (e.kind == Expr::K::Not)
            return b_.CreateNot(evalCond(*e.lhs));
        if (num_.numericExpr(e)) {
            Value *v = evalNum(e);
            return num_.kindOf(e) == NK::Int
                ? b_.CreateICmpNE(v, ConstantInt::get(i64Ty_, 0))
                : b_.CreateFCmpONE(v, ConstantFP::get(dblTy_, 0.0));
        }
        Value *t = callRt("mv_truth", i64Ty_, {ptrTy_}, {evalPtr(e)});
        return b_.CreateICmpNE(t, ConstantInt::get(i64Ty_, 0));
    }

    // ------------------------------------------------------------ statements

    BasicBlock *newBB(const char *name) {
        return BasicBlock::Create(llctx_, name, fn_);
    }

    void emitBlock(const std::vector<StmtP> &stmts) {
        for (const auto &s : stmts) emitStmt(*s);
    }

    void emitStmt(const Stmt &s) {
        tempUsed_ = 0;
        b_.SetCurrentDebugLocation(loc(s.line));
        switch (s.kind) {
        case Stmt::K::Assign: emitAssign(s); break;
        case Stmt::K::Dim:    emitDim(s);    break;
        case Stmt::K::If:     emitIf(s);     break;
        case Stmt::K::For:    emitFor(s);    break;
        case Stmt::K::Loop:   emitLoop(s);   break;
        case Stmt::K::Print:  emitPrint(s);  break;
        case Stmt::K::Call:   emitCall(s);   break;
        case Stmt::K::Return:
        case Stmt::K::Stop:
            b_.CreateBr(retBB_);
            b_.SetInsertPoint(newBB("dead"));
            break;
        }
    }

    void emitAssign(const Stmt &s) {
        const Expr &t = *s.target;
        if (t.kind == Expr::K::Var) {
            if (num_.numericVar(t.sval)) {
                Value *v = intVar(t.sval) ? evalNum(*s.value)
                                          : asDbl(*s.value);
                b_.CreateStore(v, numVarSlot(t.sval));
                return;
            }
            evalInto(*s.value, getScalar(t.sval, t.line));
            return;
        }
        // Paren target: must be a DIM'd array element.
        if (!arrayNames_.count(t.sval))
            err(t.line, "cannot assign to " + t.sval + "()");
        if (num_.numericArray(t.sval)) {
            Type *ety = arrElemTy(t.sval);
            Value *v;
            if (ety == dblTy_)          v = asDbl(*s.value);
            else if (ety == i64Ty_)     v = evalNum(*s.value);
            else /* i8, byte literal */ v = b_.CreateTrunc(
                                            evalNum(*s.value), ety);
            b_.CreateStore(v, numElemPtr(t));
            return;
        }
        Value *elem = arrayElemPtr(t);
        evalInto(*s.value, elem);
    }

    void emitDim(const Stmt &s) {
        Value *d1 = numIndex(*s.args[0]);
        Value *d2 = s.args.size() == 2 ? numIndex(*s.args[1])
                                       : ConstantInt::get(i64Ty_, 0);
        if (num_.numericArray(s.name)) {
            NumArr &a = numArrSlots(s.name);
            call1("mvx_buf_destroy", b_.CreateLoad(ptrTy_, a.ptr));
            Value *cols = b_.CreateSelect(
                b_.CreateICmpEQ(d2, ConstantInt::get(i64Ty_, 0)),
                ConstantInt::get(i64Ty_, 1), d2);
            Value *n = b_.CreateMul(d1, cols);
            uint64_t esz = a.elemTy == b_.getInt8Ty() ? 1 : 8;
            Value *nbytes =
                b_.CreateMul(n, ConstantInt::get(i64Ty_, esz));
            Value *p = callRt("mvx_buf_create", ptrTy_, {i64Ty_}, {nbytes});
            b_.CreateStore(p, a.ptr);
            b_.CreateStore(d1, a.d1);
            b_.CreateStore(d2, a.d2);
            return;
        }
        Value *slot = getArraySlot(s.name);
        Value *old = b_.CreateLoad(ptrTy_, slot);
        call1("mv_arr_destroy", old);
        Value *arr = callRt("mv_arr_create", ptrTy_, {i64Ty_, i64Ty_},
                            {d1, d2});
        b_.CreateStore(arr, slot);
    }

    void emitIf(const Stmt &s) {
        Value *c = evalCond(*s.cond);
        BasicBlock *thenBB = newBB("if.then");
        BasicBlock *elseBB = newBB("if.else");
        BasicBlock *doneBB = newBB("if.done");
        b_.CreateCondBr(c, thenBB, elseBB);
        b_.SetInsertPoint(thenBB);
        emitBlock(s.body);
        b_.CreateBr(doneBB);
        b_.SetInsertPoint(elseBB);
        emitBlock(s.elseBody);
        b_.CreateBr(doneBB);
        b_.SetInsertPoint(doneBB);
    }

    void emitFor(const Stmt &s) {
        if (num_.numericVar(s.name)) { emitForNum(s); return; }
        Value *var = getScalar(s.name, s.line);
        Value *limit = newSlot();
        Value *step = newSlot();
        evalInto(*s.from, var);
        evalInto(*s.to, limit);
        if (s.step) evalInto(*s.step, step);
        else callRt("mv_set_int", voidTy_, {ptrTy_, i64Ty_},
                    {step, ConstantInt::get(i64Ty_, 1)});
        Value *stepD = callRt("mv_get_dbl", dblTy_, {ptrTy_}, {step});
        Value *ascending =
            b_.CreateFCmpOGE(stepD, ConstantFP::get(dblTy_, 0.0));

        BasicBlock *testBB = newBB("for.test");
        BasicBlock *bodyBB = newBB("for.body");
        BasicBlock *doneBB = newBB("for.done");
        b_.CreateBr(testBB);

        b_.SetInsertPoint(testBB);
        Value *c = callRt("mv_compare", i64Ty_, {ptrTy_, ptrTy_},
                          {var, limit});
        Value *zero = ConstantInt::get(i64Ty_, 0);
        Value *cont = b_.CreateSelect(ascending,
                                      b_.CreateICmpSLE(c, zero),
                                      b_.CreateICmpSGE(c, zero));
        b_.CreateCondBr(cont, bodyBB, doneBB);

        b_.SetInsertPoint(bodyBB);
        emitBlock(s.body);
        b_.SetCurrentDebugLocation(loc(s.line));
        call3("mv_add", var, var, step);
        b_.CreateBr(testBB);

        b_.SetInsertPoint(doneBB);
    }

    // Native FOR loop for a numeric loop variable (i64 or double).
    void emitForNum(const Stmt &s) {
        bool isInt = intVar(s.name);
        Type *ty = isInt ? i64Ty_ : dblTy_;
        Value *var = numVarSlot(s.name);
        b_.CreateStore(isInt ? evalNum(*s.from) : asDbl(*s.from), var);
        Value *limit = isInt ? evalNum(*s.to) : asDbl(*s.to);
        Value *step;
        if (s.step) step = isInt ? evalNum(*s.step) : asDbl(*s.step);
        else step = isInt ? (Value *)ConstantInt::get(i64Ty_, 1)
                          : (Value *)ConstantFP::get(dblTy_, 1.0);
        Value *ascending = isInt
            ? b_.CreateICmpSGE(step, ConstantInt::get(i64Ty_, 0))
            : b_.CreateFCmpOGE(step, ConstantFP::get(dblTy_, 0.0));

        BasicBlock *testBB = newBB("for.test");
        BasicBlock *bodyBB = newBB("for.body");
        BasicBlock *doneBB = newBB("for.done");
        b_.CreateBr(testBB);

        b_.SetInsertPoint(testBB);
        Value *v = b_.CreateLoad(ty, var);
        Value *cont = isInt
            ? b_.CreateSelect(ascending, b_.CreateICmpSLE(v, limit),
                              b_.CreateICmpSGE(v, limit))
            : b_.CreateSelect(ascending, b_.CreateFCmpOLE(v, limit),
                              b_.CreateFCmpOGE(v, limit));
        b_.CreateCondBr(cont, bodyBB, doneBB);

        b_.SetInsertPoint(bodyBB);
        emitBlock(s.body);
        b_.SetCurrentDebugLocation(loc(s.line));
        Value *cur = b_.CreateLoad(ty, var);
        b_.CreateStore(isInt ? b_.CreateAdd(cur, step)
                             : b_.CreateFAdd(cur, step), var);
        b_.CreateBr(testBB);

        b_.SetInsertPoint(doneBB);
    }

    void emitLoop(const Stmt &s) {
        BasicBlock *preBB = newBB("loop.pre");
        BasicBlock *doneBB = newBB("loop.done");
        b_.CreateBr(preBB);
        b_.SetInsertPoint(preBB);
        emitBlock(s.pre);

        if (s.loopCond == Stmt::LoopCond::None) {
            b_.CreateBr(preBB);
            b_.SetInsertPoint(doneBB);
            return;
        }
        b_.SetCurrentDebugLocation(loc(s.cond->line));
        tempUsed_ = 0;
        Value *c = evalCond(*s.cond);
        BasicBlock *postBB = newBB("loop.post");
        if (s.loopCond == Stmt::LoopCond::While)
            b_.CreateCondBr(c, postBB, doneBB);
        else
            b_.CreateCondBr(c, doneBB, postBB);
        b_.SetInsertPoint(postBB);
        emitBlock(s.post);
        b_.CreateBr(preBB);
        b_.SetInsertPoint(doneBB);
    }

    void emitPrint(const Stmt &s) {
        for (size_t k = 0; k < s.args.size(); k++) {
            if (s.printTabs[k])
                callRt("mv_print_tab", voidTy_, {ptrTy_}, {ctxArg_});
            Value *p = evalPtr(*s.args[k]);
            callRt("mv_print", voidTy_, {ptrTy_, ptrTy_}, {ctxArg_, p});
        }
        if (!s.noNewline)
            callRt("mv_print_nl", voidTy_, {ptrTy_}, {ctxArg_});
    }

    void emitCall(const Stmt &s) {
        size_t n = s.args.size();
        Value *argv = eb_.CreateAlloca(ptrTy_, ConstantInt::get(i64Ty_, n ? n : 1),
                                       "argv");
        for (size_t k = 0; k < n; k++) {
            const Expr &a = *s.args[k];
            Value *p;
            if (a.kind == Expr::K::Var)
                p = getScalar(a.sval, a.line);
            else if (a.kind == Expr::K::Paren && arrayNames_.count(a.sval))
                p = arrayElemPtr(a);
            else
                p = evalPtr(a);
            Value *cell = b_.CreateGEP(ptrTy_, argv,
                                       ConstantInt::get(i64Ty_, k));
            b_.CreateStore(p, cell);
        }
        std::string sym = "mvx_sub_" + s.name;
        FunctionCallee callee =
            rt(sym.c_str(), voidTy_, {ptrTy_, i32Ty_, ptrTy_});
        b_.CreateCall(callee, {ctxArg_, ConstantInt::get(i32Ty_, (int)n),
                               argv});
    }

    // -------------------------------------------------------------- function

    void collectArrayNames(const std::vector<StmtP> &stmts) {
        for (const auto &sp : stmts) {
            const Stmt &s = *sp;
            if (s.kind == Stmt::K::Dim) arrayNames_.insert(s.name);
            collectArrayNames(s.body);
            collectArrayNames(s.elseBody);
            collectArrayNames(s.pre);
            collectArrayNames(s.post);
        }
    }

    void setupDebug(const std::string &fnName) {
        namespace fs = std::filesystem;
        fs::path p = fs::absolute(prog_.sourcePath);
        diFile_ = dib_.createFile(p.filename().string(),
                                  p.parent_path().string());
        // DWARF has no standard language code for BASIC; use the
        // vendor-reserved range.  Debuggers step by line table regardless.
        DICompileUnit *cu = dib_.createCompileUnit(
            DISourceLanguageName(dwarf::DW_LANG_lo_user), diFile_,
            "mvx 0.1.0", opts_.optLevel > 0, "", 0);
        (void)cu;

        DIType *i64d = dib_.createBasicType("INT64", 64,
                                            dwarf::DW_ATE_signed);
        DIType *dbld = dib_.createBasicType("FLOAT64", 64,
                                            dwarf::DW_ATE_float);
        DIType *chard = dib_.createBasicType("CHAR", 8,
                                             dwarf::DW_ATE_signed_char);
        DIType *strp = dib_.createPointerType(chard, 64);
        SmallVector<Metadata *, 4> members = {
            dib_.createMemberType(nullptr, "TAG", diFile_, 0, 64, 64, 0,
                                  DINode::FlagZero, i64d),
            dib_.createMemberType(nullptr, "I", diFile_, 0, 64, 64, 64,
                                  DINode::FlagZero, i64d),
            dib_.createMemberType(nullptr, "D", diFile_, 0, 64, 64, 128,
                                  DINode::FlagZero, dbld),
            dib_.createMemberType(nullptr, "S", diFile_, 0, 64, 64, 192,
                                  DINode::FlagZero, strp),
        };
        diValTy_ = dib_.createStructType(
            nullptr, "MVVALUE", diFile_, 0, 256, 64, DINode::FlagZero,
            nullptr, dib_.getOrCreateArray(members));

        DISubroutineType *spTy = dib_.createSubroutineType(
            dib_.getOrCreateTypeArray({nullptr}));
        int line = prog_.body.empty() ? 1 : prog_.body.front()->line;
        sp_ = dib_.createFunction(
            diFile_, prog_.isSubroutine ? prog_.name : "PROGRAM", fnName,
            diFile_, (unsigned)line, spTy, (unsigned)line,
            DINode::FlagPrototyped, DISubprogram::SPFlagDefinition);
        fn_->setSubprogram(sp_);
    }

    void buildFunction() {
        collectArrayNames(prog_.body);
        num_.run(prog_);

        std::string fnName;
        if (prog_.isSubroutine) {
            fnName = "mvx_sub_" + prog_.name;
            FunctionType *ft = FunctionType::get(
                voidTy_, {ptrTy_, i32Ty_, ptrTy_}, false);
            fn_ = Function::Create(ft, Function::ExternalLinkage, fnName,
                                   mod_);
        } else {
            fnName = "mvx_main";
            FunctionType *ft = FunctionType::get(voidTy_, {ptrTy_}, false);
            fn_ = Function::Create(ft, Function::ExternalLinkage, fnName,
                                   mod_);
        }
        ctxArg_ = fn_->getArg(0);
        ctxArg_->setName("ctx");

        setupDebug(fnName);

        BasicBlock *entry = newBB("entry");
        BasicBlock *start = newBB("start");
        retBB_ = BasicBlock::Create(llctx_, "ret", fn_);

        // Entry block: allocas and one-time init, then fall through.
        eb_.SetInsertPoint(entry);
        BranchInst *entryBr = eb_.CreateBr(start);
        eb_.SetInsertPoint(entryBr);   // subsequent allocas go before the br
        eb_.SetCurrentDebugLocation(loc(1));

        b_.SetInsertPoint(start);
        b_.SetCurrentDebugLocation(loc(1));

        if (prog_.isSubroutine) {
            Value *argc = fn_->getArg(1);
            Value *argv = fn_->getArg(2);
            argc->setName("argc");
            argv->setName("argv");
            callRt("mvx_arity_check", voidTy_,
                   {ptrTy_, i32Ty_, i32Ty_},
                   {stringConst(prog_.name),
                    ConstantInt::get(i32Ty_, (int)prog_.params.size()),
                    argc});
            for (size_t k = 0; k < prog_.params.size(); k++) {
                Value *cell = b_.CreateGEP(ptrTy_, argv,
                                           ConstantInt::get(i64Ty_, k));
                Value *p = b_.CreateLoad(ptrTy_, cell,
                                         prog_.params[k]);
                if (arrayNames_.count(prog_.params[k]))
                    err(1, "parameter " + prog_.params[k] +
                               " conflicts with DIM");
                scalars_[prog_.params[k]] = p;
                declareVarDebug(prog_.params[k], p,
                                prog_.body.empty() ? 1
                                    : prog_.body.front()->line);
            }
        }

        emitBlock(prog_.body);
        b_.CreateBr(retBB_);

        b_.SetInsertPoint(retBB_);
        b_.SetCurrentDebugLocation(
            loc(prog_.body.empty() ? 1 : prog_.body.back()->line));
        b_.CreateRetVoid();

        dib_.finalize();
    }
};

void CodeGen::run(const std::string &outPath) {
    i64Ty_ = Type::getInt64Ty(llctx_);
    i32Ty_ = Type::getInt32Ty(llctx_);
    dblTy_ = Type::getDoubleTy(llctx_);
    voidTy_ = Type::getVoidTy(llctx_);
    ptrTy_ = PointerType::get(llctx_, 0);
    valTy_ = StructType::create(llctx_, {i64Ty_, i64Ty_, dblTy_, ptrTy_},
                                "mv_value");

    mod_.addModuleFlag(Module::Warning, "Debug Info Version",
                       DEBUG_METADATA_VERSION);
    mod_.addModuleFlag(Module::Warning, "Dwarf Version", 4);

    buildFunction();

    std::string verifyErr;
    raw_string_ostream vos(verifyErr);
    if (verifyModule(mod_, &vos))
        report_fatal_error(Twine("internal error: invalid IR generated:\n") +
                           vos.str());

    // Target setup.
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    Triple triple(sys::getDefaultTargetTriple());
    std::string lookupErr;
    const Target *target = TargetRegistry::lookupTarget(triple, lookupErr);
    if (!target) report_fatal_error(Twine(lookupErr));
    TargetOptions topts;
    TargetMachine *tm = target->createTargetMachine(
        triple, sys::getHostCPUName(), "", topts, Reloc::PIC_);
    mod_.setDataLayout(tm->createDataLayout());
    mod_.setTargetTriple(triple);

    // Optimisation.
    if (opts_.optLevel > 0) {
        LoopAnalysisManager lam;
        FunctionAnalysisManager fam;
        CGSCCAnalysisManager cgam;
        ModuleAnalysisManager mam;
        PassBuilder pb(tm);
        pb.registerModuleAnalyses(mam);
        pb.registerCGSCCAnalyses(cgam);
        pb.registerFunctionAnalyses(fam);
        pb.registerLoopAnalyses(lam);
        pb.crossRegisterProxies(lam, fam, cgam, mam);
        ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(
            opts_.optLevel >= 2 ? OptimizationLevel::O2
                                : OptimizationLevel::O1);
        mpm.run(mod_, mam);
    }

    if (opts_.emitLLVM) {
        std::error_code ec;
        raw_fd_ostream os(outPath + ".ll", ec, sys::fs::OF_Text);
        if (!ec) mod_.print(os, nullptr);
    }

    std::error_code ec;
    raw_fd_ostream dest(outPath, ec, sys::fs::OF_None);
    if (ec)
        report_fatal_error(Twine("cannot write ") + outPath + ": " +
                           ec.message());
    legacy::PassManager emit;
    if (tm->addPassesToEmitFile(emit, dest, nullptr,
                                CodeGenFileType::ObjectFile))
        report_fatal_error("target cannot emit object files");
    emit.run(mod_);
    dest.flush();
}

} // namespace

void compileToObject(const Program &prog, const std::string &outPath,
                     const CodegenOptions &opts) {
    CodeGen cg(prog, opts);
    cg.run(outPath);
}

} // namespace mvx
