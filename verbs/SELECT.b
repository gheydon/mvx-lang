* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* SELECT file {WITH item op value {AND ...}} — form the active select list
* for the next command (the session carries it across processes).  Multiple
* WITH/AND conditions are ANDed and pushed to one SQL WHERE when possible.
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

* ---- parse WITH conditions (ANDed) ----
NW = 0
WIS = ""
WOPS = ""
WVS = ""
DESC = 0
NT = DCOUNT(S, " ")
I = TBASE
LOOP
WHILE I <= NT DO
   T = FIELD(S, " ", I)
   IF T = "WITH" OR T = "AND" THEN
      NW = NW + 1
      WIS<NW> = FIELD(S, " ", I + 1)
      WOPS<NW> = FIELD(S, " ", I + 2)
      WVS<NW> = FIELD(S, " ", I + 3)
      I = I + 3
   END
   IF T = "DESCRIBE" OR T = "EXPLAIN" THEN DESC = 1
   I = I + 1
REPEAT
FOR K = 1 TO NW
   V = WVS<K>
   IF LEN(V) >= 2 THEN
      Q = V[1, 1]
      IF Q = "'" OR Q = '"' THEN WVS<K> = V[2, LEN(V) - 2]
   END
NEXT K

* ---- resolve each condition's attribute / I-descriptor ----
WANOS = ""
WSPECS = ""
FOR K = 1 TO NW
   WI = WIS<K>
   WANO = ""
   WSPEC = ""
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
   WANOS<K> = WANO
   WSPECS<K> = WSPEC
NEXT K

* ---- DESCRIBE: show the query plan, don't run it -----------------------
IF DESC THEN
   PSPEC = ""
   FOR K = 1 TO NW
      PSPEC<K> = WANOS<K>:@VM:WOPS<K>:@VM:WVS<K>
   NEXT K
   PLAN = DESCRIBE(F, PSPEC, "0":@VM:"0":@VM:"0")
   PRINT PLAN
   STOP
END

IF SYSTEM(11) = 0 THEN
   IXUSED = 0
   IF NW >= 1 THEN
      PSPEC = ""
      FOR K = 1 TO NW
         PSPEC<K> = WANOS<K>:@VM:WOPS<K>:@VM:WVS<K>
      NEXT K
      IXUSED = MULTISELECT(F, PSPEC)
   END
   IF IXUSED = 0 AND NW = 1 THEN
      IF WANOS<1> = -1 AND WSPECS<1>[1, 6] = "TRANS(" THEN
         IXUSED = TRANSSELECT(F, WSPECS<1>, WOPS<1>, WVS<1>)
      END
      IF IXUSED = 0 AND WOPS<1> = "=" AND WANOS<1> > 0 THEN
         IXUSED = INDEXSELECT(F, WIS<1>, WVS<1>)
      END
   END
   IF IXUSED THEN
      NW = 0
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
   IF NW >= 1 THEN
      READ R FROM F, ID ELSE R = ""
      FOR K = 1 TO NW
         IF OK THEN
            BEGIN CASE
            CASE WANOS<K> = 0
               RV = ID
            CASE WANOS<K> = -1
               ISPEC = WSPECS<K>
               GOSUB 9000
               RV = IV
            CASE 1
               RV = R<WANOS<K>>
            END CASE
            WOP = WOPS<K>
            WV = WVS<K>
            CK = 0
            BEGIN CASE
            CASE WOP = "="
               IF RV = WV THEN CK = 1
            CASE WOP = "#"
               IF RV # WV THEN CK = 1
            CASE WOP = ">"
               IF RV > WV THEN CK = 1
            CASE WOP = "<"
               IF RV < WV THEN CK = 1
            CASE WOP = ">="
               IF RV >= WV THEN CK = 1
            CASE WOP = "<="
               IF RV <= WV THEN CK = 1
            END CASE
            IF CK = 0 THEN OK = 0
         END
      NEXT K
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
IF ISPEC[1, 6] = "TRANS(" AND ISPEC[LEN(ISPEC), 1] = ")" THEN
   TARGS = ISPEC[7, LEN(ISPEC) - 7]
   TFILE = FIELD(TARGS, ",", 1)
   TKA = FIELD(TARGS, ",", 2)
   TAT = FIELD(TARGS, ",", 3)
   TCT = FIELD(TARGS, ",", 4)
   IF TCT = "" THEN TCT = "X"
   IV = TRANS(TFILE, R<TKA>, TAT, TCT)
   GOTO 9090
END
9090 RETURN
