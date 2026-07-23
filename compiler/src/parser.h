#pragma once

#include "ast.h"

#include <stdexcept>
#include <string>

namespace mvx {

// Compile-time diagnostic.  Formatted on stderr by the driver as
// "item:line: message" — that format is an interface (the BASIC verb
// will parse it later), so route all errors through this.
struct CompileError : std::runtime_error {
    std::string item;
    int line;
    CompileError(std::string item_, int line_, const std::string &msg)
        : std::runtime_error(msg), item(std::move(item_)), line(line_) {}
};

Program parse(const std::string &src, const std::string &sourcePath);

} // namespace mvx
