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

IF SYSTEM(11) = 0 THEN SELECT F
IDS = ""
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   OK = 1
   IF WI # "" THEN
      READ R FROM F, ID ELSE R = ""
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
      IDS<-1> = ID
   END
REPEAT
FORMLIST IDS
PRINT DCOUNT(IDS, @AM):" record(s) selected"
