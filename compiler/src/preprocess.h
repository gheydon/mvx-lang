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

namespace mvx {

// Expand the source preprocessor: $DEFINE / $UNDEFINE / $IFDEF / $IFNDEF
// / $ELSE / $ENDIF (UniVerse/UniData style), plus macro substitution of
// defined values.  `predefined` seeds the symbol table (e.g. MVX).
// Inactive and directive lines are blanked, not removed, so reported
// line numbers and DWARF still match the original source.  Throws
// CompileError on a malformed or unbalanced directive.
std::string preprocess(const std::string &src, const std::string &item,
                       const std::map<std::string, std::string> &predefined);

} // namespace mvx
