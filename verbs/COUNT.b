* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* COUNT {DICT} file {WITH item op value}
S = TRIM(SENTENCE())
NT = DCOUNT(S, " ")
DICTF = 0
TBASE = 2
NAME = FIELD(S, " ", 2)
IF NAME = "DICT" THEN
   DICTF = 1
   NAME = FIELD(S, " ", 3)
   TBASE = 3
END
IF NAME = "" THEN
   PRINT "usage: COUNT {DICT} filename {WITH item op value}"
   STOP
END
* ---- parse an optional WITH clause ----
WI = ""
WOP = ""
WV = ""
I = TBASE + 1
LOOP
WHILE I <= NT DO
   IF FIELD(S, " ", I) = "WITH" THEN
      WI = FIELD(S, " ", I + 1)
      WOP = FIELD(S, " ", I + 2)
      WV = FIELD(S, " ", I + 3)
      I = I + 3
   END
   I = I + 1
REPEAT
IF LEN(WV) >= 2 THEN
   Q = WV[1, 1]
   IF Q = "'" OR Q = '"' THEN WV = WV[2, LEN(WV) - 2]
END
* ---- open the file (and dict, to resolve the WITH item) ----
IF DICTF THEN
   OPEN "DICT", NAME TO F ELSE
      PRINT "cannot open DICT ":NAME
      STOP
   END
   DOPEN = 0
END ELSE
   OPEN NAME TO F ELSE
      PRINT "cannot open ":NAME
      STOP
   END
   DOPEN = 1
   OPEN "DICT", NAME TO DC ELSE DOPEN = 0
END
WANO = ""
IF WI # "" THEN
   IF WI = "@ID" THEN
      WANO = 0
   END ELSE
      GOT = 0
      IF DOPEN THEN
         READ DI FROM DC, WI THEN
            IF DI<1>[1, 1] = "I" THEN WANO = -1 ELSE WANO = DI<2>
            GOT = 1
         END
      END
      IF GOT = 0 THEN
         PRINT WI:" is not a dictionary item in ":NAME
         STOP
      END
   END
END
* ---- an active select list is already the answer: count it ----
IF SYSTEM(11) = 1 THEN
   GOSUB 1000
   STOP
END
* ---- push the count into the backend when we can ----
CNT = QUERYCOUNT(F, WI, WOP, WV, WANO)
IF CNT >= 0 THEN
   PRINT CNT:" record(s) counted"
   STOP
END
* ---- fall back: form a (filtered) list and count it ----
IF WI # "" THEN
   Q = '"'
   EXECUTE "SSELECT ":NAME:" WITH ":WI:" ":WOP:" ":Q:WV:Q CAPTURING JUNK
END ELSE
   SELECT F
END
GOSUB 1000
STOP
* count the records the active select list yields
1000 N = 0
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   N = N + 1
REPEAT
PRINT N:" record(s) counted"
RETURN
