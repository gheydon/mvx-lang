#pragma once

#include "ast.h"

#include <string>

namespace mvx {

struct CodegenOptions {
    int  optLevel = 2;
    bool emitLLVM = false;      // write textual IR next to the object
};

// Compile one parsed program to a native object file.
// Throws CompileError on semantic errors.
void compileToObject(const Program &prog, const std::string &outPath,
                     const CodegenOptions &opts);

} // namespace mvx
