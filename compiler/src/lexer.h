#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mvx {

enum class Tok {
    Eof, Eol,
    Ident, IntLit, FltLit, StrLit,
    // punctuation / operators
    LParen, RParen, Comma, Semi, Colon,
    Plus, Minus, Star, Slash, Caret,
    Eq,             // '=' — assignment or equality by context
    Ne, Lt, Le, Gt, Ge,
    // keywords
    KwIf, KwThen, KwElse, KwEnd,
    KwFor, KwNext, KwTo, KwStep,
    KwLoop, KwRepeat, KwWhile, KwUntil, KwDo,
    KwDim, KwPrint, KwCrt,
    KwCall, KwSubroutine, KwReturn, KwStop,
    KwGoto, KwGo, KwGosub,
    KwBegin, KwCase, KwLocate,
    KwAnd, KwOr, KwNot,
};

struct Token {
    Tok kind;
    int line;
    std::string text;     // identifier / string literal contents
    int64_t ival = 0;
    double fval = 0.0;
};

// Lexes the whole source up front; parser indexes into the token stream.
// Throws CompileError (see parser.h) on malformed tokens.
std::vector<Token> lex(const std::string &src, const std::string &item);

} // namespace mvx
