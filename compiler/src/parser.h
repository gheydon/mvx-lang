/*
 * MVX — a native compiler and runtime for Pick/MultiValue BASIC.
 * Copyright (C) 2026 Gordon Heydon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.  There is NO WARRANTY, to
 * the extent permitted by law; see the LICENSE file for details.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "ast.h"

#include <set>
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

// extFuncs: names of package-provided extension functions (from EXPORTS
// manifests) — treated as callable functions so `X = NAME(args)` parses as a
// call, not an array reference.  codegen dispatches them to mvx_ext_invoke.
Program parse(const std::string &src, const std::string &sourcePath,
              const std::set<std::string> &extFuncs = {});

} // namespace mvx
