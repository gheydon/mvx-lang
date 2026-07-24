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
*  * @file DELETE-INDEX
*  * @version 1.0
*  */
* DELETE-INDEX file item
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
IT = FIELD(S, " ", 3)
IF FN = "" OR IT = "" THEN
   PRINT "usage: DELETE-INDEX file item"
   STOP
END
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
OPEN "DICT", FN TO DC ELSE
   PRINT FN:" has no dictionary"
   STOP
END
READ XL FROM DC, "%INDEXES%" ELSE XL = ""
LOCATE(IT, XL; POS) ELSE
   PRINT IT:" is not indexed on ":FN
   STOP
END
XL = DELETE(XL, POS, 0, 0)
WRITE XL ON DC, "%INDEXES%"
X = INDEXDROP(F, IT)
PRINT "index ":FN:".":IT:" deleted"
