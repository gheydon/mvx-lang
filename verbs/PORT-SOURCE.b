* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
/**
 * @file PORT-SOURCE
 * @version 1.0
 */
* PORT-SOURCE file item {TO file2} {item2} — rewrite MVX C-style
* comments as classic Pick comments so the source compiles on legacy
* MV platforms:
*   /* block */   ->  * lines (docblock decoration stripped)
*   code // c     ->  code ; * c
*   a /* c */ b   ->  * c  hoisted above the statement
* String literals are respected; classic comment lines pass through.
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
IT = FIELD(S, " ", 3)
T4 = FIELD(S, " ", 4)
IF FN = "" OR IT = "" THEN
   PRINT "usage: PORT-SOURCE file item {TO file2} {item2}"
   STOP
END
FO = FN
ITO = IT:".PORTED"
IF T4 = "TO" THEN
   FO = FIELD(S, " ", 5)
   ITO = FIELD(S, " ", 6)
   IF ITO = "" THEN ITO = IT
END ELSE
   IF T4 # "" THEN ITO = T4
END
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
OPEN FO TO FD ELSE
   PRINT "cannot open ":FO
   STOP
END
READ REC FROM F, IT ELSE
   PRINT IT:" not on file ":FN
   STOP
END

OUT = ""
INBLK = 0
NL = DCOUNT(REC, @AM)
FOR L = 1 TO NL
   LN = REC<L>
   TL = TRIM(LN)
   * continuation of a block comment
   IF INBLK THEN
      P = INDEX(LN, "*/", 1)
      IF P = 0 THEN
         CTEXT = LN
         GOSUB 9100
         GOTO 200
      END
      CTEXT = LN[1, P - 1]
      GOSUB 9100
      LN = LN[P + 2, LEN(LN)]
      TL = TRIM(LN)
      INBLK = 0
      IF TL = "" THEN GOTO 200
   END
   * classic comment lines pass through untouched
   IF TL[1, 1] = "*" OR TL[1, 1] = "!" THEN
      OUT<-1> = LN
      GOTO 200
   END
   IF OCONV(FIELD(TL, " ", 1), "MCU") = "REM" THEN
      OUT<-1> = LN
      GOTO 200
   END
   * scan the line: strings, //, /* ... */
   CODE = ""
   PEND = ""
   TRAILC = ""
   HASTRAIL = 0
   INSTR = 0
   QC = ""
   I = 1
   LOOP
   WHILE I <= LEN(LN) DO
      C = LN[I, 1]
      BEGIN CASE
      CASE INSTR
         CODE = CODE:C
         IF C = QC THEN INSTR = 0
      CASE C = '"' OR C = "'"
         INSTR = 1
         QC = C
         CODE = CODE:C
      CASE C = "/" AND LN[I + 1, 1] = "/"
         TRAILC = LN[I + 2, LEN(LN)]
         HASTRAIL = 1
         I = LEN(LN)
      CASE C = "/" AND LN[I + 1, 1] = "*"
         REST = LN[I + 2, LEN(LN)]
         P = INDEX(REST, "*/", 1)
         IF P = 0 THEN
            CTEXT = REST
            INBLK = 1
            I = LEN(LN)
            GOSUB 9200
            IF PTEXT # "" THEN
               PEND<-1> = PTEXT
            END
         END ELSE
            CTEXT = REST[1, P - 1]
            GOSUB 9200
            IF PTEXT # "" THEN
               PEND<-1> = PTEXT
            END
            I = I + 2 + P
         END
      CASE 1
         CODE = CODE:C
      END CASE
      I = I + 1
   REPEAT
   * emit: hoisted comments first, then the code line
   NP = DCOUNT(PEND, @AM)
   FOR K = 1 TO NP
      OUT<-1> = "* ":PEND<K>
   NEXT K
   IF TRIM(CODE) = "" THEN
      IF HASTRAIL THEN
         GOSUB 9300
      END
      IF TRIM(CODE) = "" AND HASTRAIL = 0 AND NP = 0 AND INBLK = 0 THEN
         IF TL # "" THEN OUT<-1> = CODE
         IF TL = "" THEN OUT<-1> = LN
      END
   END ELSE
      IF HASTRAIL THEN
         TT = TRIM(TRAILC)
         IF TT # "" THEN
            OUT<-1> = CODE:" ; * ":TT
         END ELSE
            OUT<-1> = CODE
         END
      END ELSE
         OUT<-1> = CODE
      END
   END
200 X = 0
NEXT L
WRITE OUT ON FD, ITO
PRINT FN:" ":IT:" ported to ":FO:" ":ITO:" (":DCOUNT(OUT, @AM):" lines)"
STOP

* ---- 9100: emit one block-comment interior line ------------------------
9100
CTEXT2 = TRIM(CTEXT)
LOOP
WHILE CTEXT2[1, 1] = "*" DO
   CTEXT2 = TRIM(CTEXT2[2, LEN(CTEXT2)])
REPEAT
IF CTEXT2 = "" THEN
   OUT<-1> = "*"
END ELSE
   OUT<-1> = "* ":CTEXT2
END
RETURN

* ---- 9200: clean inline comment text into PTEXT ------------------------
9200
PTEXT = TRIM(CTEXT)
LOOP
WHILE PTEXT[1, 1] = "*" DO
   PTEXT = TRIM(PTEXT[2, LEN(PTEXT)])
REPEAT
RETURN

* ---- 9300: comment-only line with trailing // --------------------------
9300
TT = TRIM(TRAILC)
IF TT = "" THEN
   OUT<-1> = "*"
END ELSE
   OUT<-1> = "* ":TT
END
RETURN
