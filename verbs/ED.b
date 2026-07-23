* /**
*  * @file ED
*  * @version 1.0
*  */
* ED file id — the classic Pick line editor.
*   <Enter>      next line          n        go to line n
*   L {n}        list n lines       T / B    top / bottom
*   I            insert after current line ("." alone ends input)
*   DE {n}       delete n lines from current
*   R/old/new    replace in current line
*   FI           file (save) and exit
*   EX           exit without saving
*   ?            help
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
ID = FIELD(S, " ", 3)
IF FN = "" OR ID = "" THEN
   PRINT "usage: ED file id"
   STOP
END
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
NEW = 0
READ REC FROM F, ID ELSE
   REC = ""
   NEW = 1
END
IF NEW THEN
   PRINT "new record"
END ELSE
   PRINT DCOUNT(REC, @AM):" line(s)"
END
CL = 0
LOOP
   NL = DCOUNT(REC, @AM)
   PRINT FMT(CL, "R%3"):"= ":
   INPUT CMDL
   CMDL = TRIM(CMDL)
   C1 = OCONV(FIELD(CMDL, " ", 1), "MCU")
   C2 = FIELD(CMDL, " ", 2)
   BEGIN CASE
   CASE CMDL = ""
      IF CL < NL THEN
         CL = CL + 1
         GOSUB 9000
      END ELSE
         PRINT "EOI"
      END
   CASE NUM(C1)
      CL = C1
      IF CL < 0 THEN CL = 0
      IF CL > NL THEN CL = NL
      IF CL > 0 THEN GOSUB 9000
   CASE C1 = "T"
      CL = 0
      PRINT "top"
   CASE C1 = "B"
      CL = NL
      IF CL > 0 THEN GOSUB 9000
   CASE C1 = "L"
      LC = C2
      IF NUM(LC) = 0 THEN LC = 22
      IF LC < 1 THEN LC = 22
      FOR K = 1 TO LC
         IF CL >= NL THEN
            PRINT "EOI"
            K = LC
         END ELSE
            CL = CL + 1
            GOSUB 9000
         END
      NEXT K
   CASE C1 = "I"
      LOOP
         PRINT FMT(CL + 1, "R%3"):"+ ":
         INPUT LN
      UNTIL LN = "." DO
         REC = INSERT(REC, CL + 1, 0, 0, LN)
         CL = CL + 1
      REPEAT
   CASE C1 = "DE"
      IF CL < 1 THEN
         PRINT "no current line"
      END ELSE
         DC = C2
         IF NUM(DC) = 0 THEN DC = 1
         IF DC < 1 THEN DC = 1
         FOR K = 1 TO DC
            IF CL <= DCOUNT(REC, @AM) THEN
               REC = DELETE(REC, CL, 0, 0)
            END
         NEXT K
         IF CL > DCOUNT(REC, @AM) THEN CL = DCOUNT(REC, @AM)
         PRINT DC:" line(s) deleted"
      END
   CASE C1[1, 2] = "R/"
      IF CL < 1 THEN
         PRINT "no current line"
      END ELSE
         OLD = FIELD(CMDL, "/", 2)
         NEWS = FIELD(CMDL, "/", 3)
         LN0 = REC<CL>
         P = INDEX(LN0, OLD, 1)
         IF P = 0 OR OLD = "" THEN
            PRINT "no match"
         END ELSE
            REC<CL> = LN0[1, P - 1]:NEWS:LN0[P + LEN(OLD), LEN(LN0)]
            GOSUB 9000
         END
      END
   CASE C1 = "FI"
      WRITE REC ON F, ID
      PRINT "'":ID:"' filed"
      STOP
   CASE C1 = "EX"
      PRINT "'":ID:"' exited"
      STOP
   CASE C1 = "?"
      PRINT "Enter=next  n=goto  L {n}=list  T=top  B=bottom"
      PRINT "I=insert (. ends)  DE {n}=delete  R/old/new  FI=save  EX=quit"
   CASE 1
      PRINT "? (use ? for help)"
   END CASE
REPEAT

9000 PRINT FMT(CL, "R%3"):" ":REC<CL>
RETURN
