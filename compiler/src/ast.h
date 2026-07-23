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
        Extract,    // lhs<args>: dynamic-array extraction (1-3 subscripts)
        Substr,     // lhs[start,len]: substring; args = start, len
        Fmt,        // lhs "mask": format-mask application; rhs = mask expr
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
        Label,      // name: numeric statement label
        Goto,       // name: target label
        Gosub,      // name: target label
        Locate,     // args: item, dyn[, attr[, val]]; name: setting var;
                    // value: optional order; body/elseBody: THEN/ELSE
        Input,      // target: Var or array element read from stdin
        Mat,        // MAT name = value  |  MAT name = MAT name2
        Common,     // args: Var/Paren items; name2: block name ("" unnamed)
        Echo,       // name: "ON" or "OFF"
        Open,       // args: [dict,] spec; name: file var; THEN/ELSE
        ReadF,      // target: record var; args: file, id; name: "U" locks
        WriteF,     // value: record; args: file, id; name: "U" keeps lock
        DeleteF,    // args: file, id
        Release,    // args: [file, id] — bare releases all
        Select,     // args: file
        Readnext,   // name: id var; THEN/ELSE
        Execute,    // value: sentence; name: CAPTURING var; name2: RETURNING
        Formlist,   // value: dynamic array -> active select list
    };
    enum class LoopCond { None, While, Until };

    K kind;
    int line = 0;

    ExprP target, value;                 // Assign
    std::string name;                    // Dim / For var / Call name
    std::string name2;                   // Mat copy source array
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
