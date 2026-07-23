// AST for the Slice 1 subset of MVX BASIC.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace mvx {

struct Expr;
struct Stmt;
using ExprP = std::unique_ptr<Expr>;
using StmtP = std::unique_ptr<Stmt>;

enum class BinOp {
    Add, Sub, Mul, Div, Pow, Cat,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or,
};

struct Expr {
    enum class K {
        IntLit, FltLit, StrLit,
        Var,        // name
        Paren,      // name(args): array element or intrinsic, resolved in codegen
        Bin, Neg, Not,
    };
    K kind;
    int line = 0;

    int64_t ival = 0;
    double fval = 0.0;
    std::string sval;           // StrLit text / Var / Paren name
    std::vector<ExprP> args;    // Paren subscripts or intrinsic args
    BinOp op = BinOp::Add;
    ExprP lhs, rhs;             // Bin; Neg/Not use lhs only
};

struct Stmt {
    enum class K {
        Assign,     // target (Var or Paren) = value
        Dim,        // name, dims (1 or 2)
        If,         // cond, thenS, elseS
        For,        // var, from, to, step?, body
        Loop,       // pre, condKind, cond, post
        Print,      // items + seps, noNewline  (CRT is the same in Slice 1)
        Call,       // name, args
        Return, Stop,
    };
    enum class LoopCond { None, While, Until };

    K kind;
    int line = 0;

    ExprP target, value;                 // Assign
    std::string name;                    // Dim / For var / Call name
    std::vector<ExprP> args;             // Dim dims / Call args / Print items
    std::vector<bool> printTabs;         // Print: item k preceded by comma zone
    bool noNewline = false;              // Print trailing ':'
    ExprP cond, from, to, step;
    std::vector<StmtP> body, elseBody;   // For/If-then; If-else
    std::vector<StmtP> pre, post;        // Loop
    LoopCond loopCond = LoopCond::None;
};

struct Program {
    bool isSubroutine = false;
    std::string name;                    // subroutine name; empty for main
    std::vector<std::string> params;
    std::vector<StmtP> body;
    std::string sourcePath;
};

} // namespace mvx
