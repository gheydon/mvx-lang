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
CONVS = ""
HEADS = ""
MASKS = ""
BAD = ""
FOR C = 1 TO CN
   NM = COLS<C>
   ANO = ""
   CV = ""
   HD = NM
   MK = "L#10"
   IF NM = "@ID" THEN
      ANO = 0
   END ELSE
      GOT = 0
      IF DOPEN THEN
         READ DI FROM DC, NM THEN
            ANO = DI<2>
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
            GOT = 1
         END
      END
      IF GOT = 0 THEN
         BAD = NM
      END
   END
   ANOS<C> = ANO
   CONVS<C> = CV
   HEADS<C> = HD
   MASKS<C> = MK
NEXT C
IF BAD # "" THEN
   PRINT BAD:" is not a dictionary item in ":FN
   STOP
END

* WITH item resolution
WANO = ""
IF WI # "" THEN
   IF WI = "@ID" THEN
      WANO = 0
   END ELSE
      GOT = 0
      IF DOPEN THEN
         READ DI FROM DC, WI THEN
            WANO = DI<2>
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
BORD = "AL"
IF BYI # "" THEN
   IF BYI = "@ID" THEN
      BANO = 0
   END ELSE
      GOT = 0
      IF DOPEN THEN
         READ DI FROM DC, BYI THEN
            BANO = DI<2>
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
IF SYSTEM(11) = 0 THEN SELECT F
IDS = ""
KEYS = ""
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   READ R FROM F, ID ELSE R = ""
   OK = 1
   IF WI # "" THEN
      IF WANO = 0 THEN
         RV = ID
      END ELSE
         RV = R<WANO>
      END
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
         IF BANO = 0 THEN
            K = ID
         END ELSE
            K = R<BANO>
         END
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
   ROW = FMT(ID, "L#12")
   FOR C = 1 TO CN
      IF ANOS<C> = 0 THEN
         V = ID
      END ELSE
         V = R<ANOS<C>>
      END
      IF CONVS<C> # "" THEN
         V = OCONV(V, CONVS<C>)
      END
      ROW = ROW:" ":FMT(V, MASKS<C>)
   NEXT C
   PRINT ROW
NEXT K
PRINT N:" record(s) listed"
