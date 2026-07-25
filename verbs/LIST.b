* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* LIST file {items...} {WITH item op value} {BY item}
* Columns come from dictionary D-items:
*   1 = D, 2 = attribute number, 3 = conversion (OCONV code),
*   4 = column heading, 5 = format e.g. "12L" / "8R".
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
   PRINT "usage: LIST {DICT} file {items} {WITH item op value} {BY item}"
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
* column definitions come from the file's dictionary; a dictionary
* listing has no dict-of-dict, so only @ID resolves there
DOPEN = 0
IF DICTF = 0 THEN
   DOPEN = 1
   OPEN "DICT", FN TO DC ELSE DOPEN = 0
END

* ---- parse the sentence ------------------------------------------------
NT = DCOUNT(S, " ")
COLS = ""
WI = ""
WOP = ""
WV = ""
BYI = ""
I = TBASE
LOOP
WHILE I <= NT DO
   T = FIELD(S, " ", I)
   BEGIN CASE
   CASE T = "WITH"
      WI = FIELD(S, " ", I + 1)
      WOP = FIELD(S, " ", I + 2)
      WV = FIELD(S, " ", I + 3)
      I = I + 3
   CASE T = "BY"
      BYI = FIELD(S, " ", I + 1)
      I = I + 1
   CASE 1
      COLS<-1> = T
   END CASE
   I = I + 1
REPEAT
IF LEN(WV) >= 2 THEN
   Q = WV[1, 1]
   IF Q = "'" OR Q = '"' THEN
      WV = WV[2, LEN(WV) - 2]
   END
END

* ---- resolve dictionary items ------------------------------------------
* Column layout: parallel dynamic arrays; slot 0 conventions: attr no 0
* means the record id itself.
CN = DCOUNT(COLS, @AM)
ANOS = ""
ISPECS = ""
CONVS = ""
HEADS = ""
MASKS = ""
ASSOCS = ""
BAD = ""
FOR C = 1 TO CN
   NM = COLS<C>
   ANO = ""
   ISP = ""
   CV = ""
   HD = NM
   MK = "L#10"
   ASN = ""
   IF NM = "@ID" THEN
      ANO = 0
   END ELSE
      GOT = 0
      IF DOPEN THEN
         READ DI FROM DC, NM THEN
            IF DI<1>[1, 1] = "I" THEN
               ANO = -1
               ISP = DI<2>
            END ELSE
               ANO = DI<2>
            END
            CV = DI<3>
            IF DI<4> # "" THEN
               HD = DI<4>
            END
            FM = DI<5>
            IF FM # "" THEN
               J = FM[LEN(FM), 1]
               W = FM[1, LEN(FM) - 1]
               IF J = "R" THEN
                  MK = "R#":W
               END ELSE
                  MK = "L#":W
               END
            END
            ASN = DI<6>
            GOT = 1
         END
      END
      IF GOT = 0 THEN
         BAD = NM
      END
   END
   ANOS<C> = ANO
   ISPECS<C> = ISP
   CONVS<C> = CV
   HEADS<C> = HD
   MASKS<C> = MK
   ASSOCS<C> = ASN
NEXT C
IF BAD # "" THEN
   PRINT BAD:" is not a dictionary item in ":FN
   STOP
END

* WITH item resolution
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

* BY item resolution; the dict item's justification decides the sort
* order — R-formatted fields sort right-justified (numeric).
BANO = ""
BSPEC = ""
BORD = "AL"
IF BYI # "" THEN
   IF BYI = "@ID" THEN
      BANO = 0
   END ELSE
      GOT = 0
      IF DOPEN THEN
         READ DI FROM DC, BYI THEN
            IF DI<1>[1, 1] = "I" THEN
               BANO = -1
               BSPEC = DI<2>
            END ELSE
               BANO = DI<2>
            END
            FM = DI<5>
            IF FM # "" THEN
               IF FM[LEN(FM), 1] = "R" THEN
                  BORD = "AR"
               END
            END
            GOT = 1
         END
      END
      IF GOT = 0 THEN
         PRINT BYI:" is not a dictionary item in ":FN
         STOP
      END
   END
END

* ---- scan, filter, order -----------------------------------------------
* use the active select list when one exists, classic style
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
KEYS = ""
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   READ R FROM F, ID ELSE R = ""
   OK = 1
   * control records (%FILE%, %INDEXES%, ...) are metadata, not fields
   IF DICTF AND ID[1, 1] = "%" THEN OK = 0
   IF OK AND WI # "" THEN
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
      IF BYI # "" THEN
         BEGIN CASE
         CASE BANO = 0
            K = ID
         CASE BANO = -1
            ISPEC = BSPEC
            GOSUB 9000
            K = IV
         CASE 1
            K = R<BANO>
         END CASE
         LOCATE(K, KEYS; POS; BORD) THEN
            KEYS = INSERT(KEYS, POS, 0, 0, K)
            IDS = INSERT(IDS, POS, 0, 0, ID)
         END ELSE
            KEYS = INSERT(KEYS, POS, 0, 0, K)
            IDS = INSERT(IDS, POS, 0, 0, ID)
         END
      END ELSE
         IDS<-1> = ID
      END
   END
REPEAT

* ---- report ------------------------------------------------------------
HDR = FMT("@ID", "L#12")
FOR C = 1 TO CN
   HDR = HDR:" ":FMT(HEADS<C>, MASKS<C>)
NEXT C
PRINT HDR
N = DCOUNT(IDS, @AM)
FOR K = 1 TO N
   ID = IDS<K>
   READ R FROM F, ID ELSE R = ""
   * gather each column's raw value and its value count
   CVAL = ""
   VCNT = ""
   FOR C = 1 TO CN
      BEGIN CASE
      CASE ANOS<C> = 0
         V = ID
      CASE ANOS<C> = -1
         ISPEC = ISPECS<C>
         GOSUB 9000
         V = IV
      CASE 1
         V = R<ANOS<C>>
      END CASE
      CVAL<C> = V
      VCNT<C> = DCOUNT(V, @VM)
   NEXT C
   * a column in an association takes its value count from the
   * controlling member (the one with the lowest attribute number), so
   * dependents align to it.  NV is the tallest column: one sub-row per
   * value, single-valued columns shown once and blank thereafter.
   NV = 1
   FOR C = 1 TO CN
      EC = VCNT<C>
      IF ASSOCS<C> # "" THEN
         CTL = C
         FOR C2 = 1 TO CN
            IF ASSOCS<C2> = ASSOCS<C> AND ANOS<C2> < ANOS<CTL> THEN
               CTL = C2
            END
         NEXT C2
         EC = VCNT<CTL>
      END
      ECNT<C> = EC
      IF EC > NV THEN
         NV = EC
      END
   NEXT C
   FOR SR = 1 TO NV
      IF SR = 1 THEN
         ROW = FMT(ID, "L#12")
      END ELSE
         ROW = FMT("", "L#12")
      END
      FOR C = 1 TO CN
         IF SR <= ECNT<C> THEN
            V = FIELD(CVAL<C>, @VM, SR)
         END ELSE
            V = ""
         END
         IF V # "" AND CONVS<C> # "" THEN
            V = OCONV(V, CONVS<C>)
         END
         ROW = ROW:" ":FMT(V, MASKS<C>)
      NEXT C
      PRINT ROW
   NEXT SR
NEXT K
PRINT N:" record(s) listed"
STOP

* ---- I-descriptor evaluation -------------------------------------------
* Spec in ISPEC, current record in R; result in IV.
* Supported: DOCTAG(tag) — scan comment lines (* or !) for the docblock
* annotation "@tag value" and return the value.
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
