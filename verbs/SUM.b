* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* SUM {DICT} file field {WITH item op value} — total a numeric field.
* When the field is mapped to a NUMERIC column the total is computed in the
* backend (SELECT sum(col), optionally filtered); otherwise every record is
* read and the field OCONV'd and added.
S = TRIM(SENTENCE())
NT = DCOUNT(S, " ")
DICTF = 0
TBASE = 2
FN = FIELD(S, " ", 2)
IF FN = "DICT" THEN
   DICTF = 1
   FN = FIELD(S, " ", 3)
   TBASE = 3
END
SF = FIELD(S, " ", TBASE + 1)
IF FN = "" OR SF = "" THEN
   PRINT "usage: SUM {DICT} file field {WITH item op value}"
   STOP
END
* ---- optional WITH ----
WI = ""
WOP = ""
WV = ""
I = TBASE + 2
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
* ---- open + resolve ----
IF DICTF THEN
   OPEN "DICT", FN TO F ELSE
      PRINT "cannot open DICT ":FN
      STOP
   END
   DOPEN = 0
END ELSE
   OPEN FN TO F ELSE
      PRINT "cannot open ":FN
      STOP
   END
   DOPEN = 1
   OPEN "DICT", FN TO DC ELSE DOPEN = 0
END
IF DOPEN = 0 THEN
   PRINT FN:" has no dictionary"
   STOP
END
READ SDI FROM DC, SF ELSE
   PRINT SF:" is not a dictionary item in ":FN
   STOP
END
IF SDI<1>[1, 1] # "D" THEN
   PRINT SF:" is not a summable attribute"
   STOP
END
SANO = SDI<2>
SCONV = SDI<3>
WANO = ""
IF WI # "" THEN
   IF WI = "@ID" THEN
      WANO = 0
   END ELSE
      READ WDI FROM DC, WI ELSE
         PRINT WI:" is not a dictionary item in ":FN
         STOP
      END
      IF WDI<1>[1, 1] = "I" THEN WANO = -1 ELSE WANO = WDI<2>
   END
END
* ---- push the sum into the backend when we can ----
IF SYSTEM(11) = 0 THEN
   R = QUERYSUM(F, SF, WI, WOP, WV, WANO)
   IF R # "" THEN
      PRINT SF:" total = ":R
      STOP
   END
   * fall back: form a (filtered) list to scan
   IF WI # "" THEN
      Q = '"'
      EXECUTE "SSELECT ":FN:" WITH ":WI:" ":WOP:" ":Q:WV:Q CAPTURING JUNK
   END ELSE
      SELECT F
   END
END
* ---- scan and total the OCONV'd field ----
T = 0
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   READ REC FROM F, ID THEN
      V = REC<SANO>
      IF SCONV # "" THEN V = OCONV(V, SCONV)
      IF NUM(V) THEN T = T + V
   END
REPEAT
PRINT SF:" total = ":T
