* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* MAP-MODE file {native|mirror} — show or change a mapping's write policy.
* mirror (default): the record blob is the source of truth; the projection
* is best-effort and a bad value stores NULL.  native: the typed columns
* are authoritative, so a WRITE whose value does not fit its column is
* rejected (ON ERROR).  Switching to native first checks that every
* existing record already fits.
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
MODE = FIELD(S, " ", 3)
IF FN = "" THEN
   PRINT "usage: MAP-MODE file {native|mirror}"
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
READ CUR FROM DD, "%MAPMODE%" ELSE CUR = "mirror"
IF MODE = "" THEN
   PRINT FN:" mapping mode: ":CUR
   STOP
END
MODE = OCONV(MODE, "MCL")
BEGIN CASE
CASE MODE = "mirror"
   WRITE "mirror" ON DD, "%MAPMODE%"
   PRINT FN:" mapping mode: mirror"
CASE MODE = "native"
   OPEN FN TO F ELSE
      PRINT "cannot open ":FN
      STOP
   END
   BAD = MAPCHECK(F, SPEC)
   BEGIN CASE
   CASE BAD = -2
      PRINT "backend does not support mapping"
   CASE BAD > 0
      PRINT BAD:" record(s) do not fit the mapping; still mirror"
   CASE 1
      WRITE "native" ON DD, "%MAPMODE%"
      PRINT FN:" mapping mode: native"
   END CASE
CASE 1
   PRINT "mode must be native or mirror"
END CASE
