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

#include <map>
#include <string>
#include <vector>

namespace mvx {

// Where an output line came from, so errors and DWARF still point at the
// original source even after $INCLUDE splices other files in.
struct PPLine {
    std::string file;   // source file the line came from
    int         line;   // its line number within that file
    int         dwarf;  // line to attribute in the compilation unit:
                        // the real line for the main file, or the
                        // include site's line for included content
};

struct PPResult {
    std::string          text;   // expanded source
    std::vector<PPLine>  map;    // one entry per output line (1-based)
};

// Expand the source preprocessor: $DEFINE / $UNDEFINE / $IFDEF / $IFNDEF
// / $ELSE / $ENDIF (UniVerse/UniData style), $INCLUDE / $INSERT source
// inclusion, plus macro substitution of defined values.  `predefined`
// seeds the symbol table (e.g. MVX).  `path` is the including file's
// path, used both as the error label and to resolve relative includes.
// The returned map lets the caller keep error and DWARF line numbers
// pointing at the real source across includes.  Throws CompileError on a
// malformed or unbalanced directive or an unreadable include.
PPResult preprocess(const std::string &src, const std::string &path,
                    const std::map<std::string, std::string> &predefined);

} // namespace mvx
