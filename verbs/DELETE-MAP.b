* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* DELETE-MAP file — drop the file's relational mapping: tear down the
* projection (columns + child tables) and remove %MAP%, so writes are no
* longer mirrored.
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
IF FN = "" THEN
   PRINT "usage: DELETE-MAP file"
   STOP
END
OPEN "DICT", FN TO DD ELSE
   PRINT "cannot open DICT ":FN
   STOP
END
READ SPEC FROM DD, "%MAP%" ELSE
   PRINT FN:" has no mapping"
   STOP
END
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
RC = MAPDROP(F, SPEC)
BEGIN CASE
CASE RC = -2
   PRINT "backend does not support mapping"
CASE RC < 0
   PRINT "map drop failed"
CASE 1
   DELETE DD, "%MAP%"
   PRINT "mapping dropped for ":FN
END CASE
