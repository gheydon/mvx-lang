* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* /**
*  * @file COPY
*  * @version 1.0
*  */
* COPY file id TO {file2} id2 — copy a record.
S = TRIM(SENTENCE())
F1 = FIELD(S, " ", 2)
ID1 = FIELD(S, " ", 3)
TW = FIELD(S, " ", 4)
A = FIELD(S, " ", 5)
B = FIELD(S, " ", 6)
IF F1 = "" OR ID1 = "" OR TW # "TO" OR A = "" THEN
   PRINT "usage: COPY file id TO {file2} id2"
   STOP
END
IF B = "" THEN
   F2 = F1
   ID2 = A
END ELSE
   F2 = A
   ID2 = B
END
OPEN F1 TO SRC ELSE
   PRINT "cannot open ":F1
   STOP
END
OPEN F2 TO DST ELSE
   PRINT "cannot open ":F2
   STOP
END
READ R FROM SRC, ID1 ELSE
   PRINT ID1:" not on file ":F1
   STOP
END
WRITE R ON DST, ID2
PRINT "1 record copied to ":F2:" ":ID2
