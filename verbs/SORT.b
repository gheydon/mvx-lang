* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* SORT file {items...} {WITH item op value} {BY item}
* Identical to LIST but sorted: with no explicit BY clause the records
* come out ordered by item-id (classic Pick default) rather than in
* hash-file order.  Columns come from dictionary D-items:
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
   PRINT "usage: SORT {DICT} file {items} {WITH item op value} {BY item}"
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
LIMIT = ""
DESC = 0
NW = 0
WIS = ""
WOPS = ""
WVS = ""
I = TBASE
LOOP
WHILE I <= NT DO
   T = FIELD(S, " ", I)
   BEGIN CASE
   CASE T = "WITH" OR T = "AND"
      NW = NW + 1
      WIS<NW> = FIELD(S, " ", I + 1)
      WOPS<NW> = FIELD(S, " ", I + 2)
      WVS<NW> = FIELD(S, " ", I + 3)
      I = I + 3
   CASE T = "DESCRIBE" OR T = "EXPLAIN"
      DESC = 1
   CASE T = "BY"
      BYI = FIELD(S, " ", I + 1)
      I = I + 1
   CASE T = "FIRST" OR T = "SAMPLE"
      LIMIT = FIELD(S, " ", I + 1)
      I = I + 1
   CASE 1
      COLS<-1> = T
   END CASE
   I = I + 1
REPEAT
FOR K = 1 TO NW
   V = WVS<K>
   IF LEN(V) >= 2 THEN
      Q = V[1, 1]
      IF Q = "'" OR Q = '"' THEN WVS<K> = V[2, LEN(V) - 2]
   END
NEXT K

* the SORT default: order by item-id unless the caller named a BY key
IF BYI = "" THEN
   BYI = "@ID"
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

* WITH item resolution — one attribute (or I-descriptor) per condition
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
LN = 0
IF LIMIT # "" THEN
   IF NUM(LIMIT) THEN LN = LIMIT
END

* ---- DESCRIBE: show the query plan, don't run it -----------------------
IF DESC THEN
   PSPEC = ""
   FOR K = 1 TO NW
      PSPEC<K> = WANOS<K>:@VM:WOPS<K>:@VM:WVS<K>
   NEXT K
   BB = 0
   IF BYI # "" AND BANO > 0 THEN BB = BANO
   BNUM = 0
   IF BORD = "AR" THEN BNUM = 1
   OSPEC = BB:@VM:BNUM:@VM:LN
   PLAN = DESCRIBE(F, PSPEC, OSPEC)
   PRINT PLAN
   STOP
END

PUSHED = 0
IF SYSTEM(11) = 0 THEN
   * BY (+ one WITH) + FIRST -> push ORDER BY / LIMIT when the order field is a
   * mapped column whose type matches the sort order; the list comes back
   * already sorted and limited, so skip the client sort and filter.  (A
   * multi-condition WITH can't combine with the ordered filter yet, so it
   * pushes the filter alone and sorts/caps in the verb.)
   IF NW <= 1 AND BYI # "" AND BANO # "" AND BANO # 0 AND BANO # -1 THEN
      BNUM = 0
      IF BORD = "AR" THEN BNUM = 1
      IF NW = 1 THEN
         PUSHED = ORDERSELECT(F, WIS<1>, WOPS<1>, WVS<1>, WANOS<1>, BANO, BNUM, LN)
      END ELSE
         PUSHED = ORDERSELECT(F, "", "", "", "", BANO, BNUM, LN)
      END
      IF PUSHED THEN
         NW = 0
         BYI = ""
      END
   END
   IF PUSHED = 0 THEN
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
* FIRST n: the push-down already limited; a client sort caps here
IF LN > 0 AND N > LN THEN N = LN
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
