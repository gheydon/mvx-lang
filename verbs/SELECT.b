* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* SELECT file {WITH item op value} — form the active select list for
* the next command (the session carries it across processes).
S = TRIM(SENTENCE())
DICTF = 0
FN = FIELD(S, " ", 2)
TBASE = 3
IF FN = "DICT" THEN
   DICTF = 1
   FN = FIELD(S, " ", 3)
   TBASE = 4
END
IF FN = "" THEN
   PRINT "usage: SELECT {DICT} file {WITH item op value}"
   STOP
END
IF DICTF THEN
   OPEN "DICT", FN TO F ELSE
      PRINT "cannot open DICT ":FN
      STOP
   END
END ELSE
   OPEN FN TO F ELSE
      PRINT "cannot open ":FN
      STOP
   END
END
DOPEN = 0
IF DICTF = 0 THEN
   DOPEN = 1
   OPEN "DICT", FN TO DC ELSE DOPEN = 0
END

WI = ""
WOP = ""
WV = ""
NT = DCOUNT(S, " ")
I = TBASE
LOOP
WHILE I <= NT DO
   T = FIELD(S, " ", I)
   IF T = "WITH" THEN
      WI = FIELD(S, " ", I + 1)
      WOP = FIELD(S, " ", I + 2)
      WV = FIELD(S, " ", I + 3)
      I = I + 3
   END
   I = I + 1
REPEAT
IF LEN(WV) >= 2 THEN
   Q = WV[1, 1]
   IF Q = "'" OR Q = '"' THEN
      WV = WV[2, LEN(WV) - 2]
   END
END

WANO = ""
WSPEC = ""
IF WI # "" THEN
   IF WI = "@ID" THEN
      WANO = 0
   END ELSE
      GOT = 0
      IF DOPEN THEN
         READ DI FROM DC, WI THEN
            IF DI<1>[1, 1] = "I" THEN
               WANO = -1
               WSPEC = DI<2>
            END ELSE
               WANO = DI<2>
            END
            GOT = 1
         END
      END
      IF GOT = 0 THEN
         PRINT WI:" is not a dictionary item in ":FN
         STOP
      END
   END
END

IF SYSTEM(11) = 0 THEN
   IXUSED = 0
   IF WI # "" AND WOP = "=" THEN
      IF WANO # "" AND WANO # 0 AND WANO # -1 THEN
         IXUSED = INDEXSELECT(F, WI, WV)
      END
   END
   IF IXUSED THEN
      WI = ""
   END ELSE
      SELECT F
   END
END
IDS = ""
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   OK = 1
   IF WI # "" THEN
      READ R FROM F, ID ELSE R = ""
      BEGIN CASE
      CASE WANO = 0
         RV = ID
      CASE WANO = -1
         ISPEC = WSPEC
         GOSUB 9000
         RV = IV
      CASE 1
         RV = R<WANO>
      END CASE
      OK = 0
      BEGIN CASE
      CASE WOP = "="
         IF RV = WV THEN OK = 1
      CASE WOP = "#"
         IF RV # WV THEN OK = 1
      CASE WOP = ">"
         IF RV > WV THEN OK = 1
      CASE WOP = "<"
         IF RV < WV THEN OK = 1
      CASE WOP = ">="
         IF RV >= WV THEN OK = 1
      CASE WOP = "<="
         IF RV <= WV THEN OK = 1
      END CASE
   END
   IF OK THEN
      IDS<-1> = ID
   END
REPEAT
FORMLIST IDS
PRINT DCOUNT(IDS, @AM):" record(s) selected"
STOP

* ---- I-descriptor evaluation (see LIST) --------------------------------
9000 IV = ""
IF ISPEC[1, 7] = "DOCTAG(" AND ISPEC[LEN(ISPEC), 1] = ")" THEN
   TAG = ISPEC[8, LEN(ISPEC) - 8]
   IF LEN(TAG) >= 2 THEN
      TQ = TAG[1, 1]
      IF TQ = "'" OR TQ = '"' THEN
         TAG = TAG[2, LEN(TAG) - 2]
      END
   END
   NA = DCOUNT(R, @AM)
   FOR IL = 1 TO NA
      LN = TRIM(R<IL>)
      IF LN[1, 1] = "*" OR LN[1, 1] = "!" THEN
         IP = INDEX(LN, "@":TAG:" ", 1)
         IF IP > 0 THEN
            IV = TRIM(LN[IP + LEN(TAG) + 2, LEN(LN)])
            GOTO 9090
         END
      END
   NEXT IL
END
9090 RETURN
