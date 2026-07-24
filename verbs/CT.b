* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* CT {DICT} file id — copy record to terminal, classic numbered format
S = TRIM(SENTENCE())
NAME = FIELD(S, " ", 2)
DICTF = 0
IDPOS = 3
IF NAME = "DICT" THEN
   DICTF = 1
   NAME = FIELD(S, " ", 3)
   IDPOS = 4
END
ID = FIELD(S, " ", IDPOS)
IF NAME = "" OR ID = "" THEN
   PRINT "usage: CT {DICT} filename id"
   STOP
END
IF DICTF THEN
   OPEN "DICT", NAME TO F ELSE
      PRINT "cannot open DICT ":NAME
      STOP
   END
END ELSE
   OPEN NAME TO F ELSE
      PRINT "cannot open ":NAME
      STOP
   END
END
READ R FROM F, ID THEN
   PRINT ID
   FOR I = 1 TO DCOUNT(R, @AM)
      PRINT FMT(I, "R%3"):" ":R<I>
   NEXT I
END ELSE
   PRINT ID:" not on file"
END
