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
*  * @file DELETE
*  * @version 1.0
*  */
* DELETE file id {id ...} — delete records.
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
IF FN = "" OR FIELD(S, " ", 3) = "" THEN
   PRINT "usage: DELETE file id {id ...}"
   STOP
END
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
N = 0
NT = DCOUNT(S, " ")
FOR I = 3 TO NT
   ID = FIELD(S, " ", I)
   READ R FROM F, ID THEN
      DELETE F, ID
      N = N + 1
   END ELSE
      PRINT ID:" not on file"
   END
NEXT I
PRINT N:" record(s) deleted"
