* CATALOG file item — link a program into CATALOG/ and publish it in VOC.
S = SENTENCE()
FN = FIELD(S, " ", 2)
IT = FIELD(S, " ", 3)
IF FN = "" OR IT = "" THEN
   PRINT "usage: CATALOG filename itemname"
   STOP
END
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
READ SRC FROM F, IT ELSE
   PRINT IT:" not on file ":FN
   STOP
END
* A SUBROUTINE source catalogs into LIB/ as a shared library the
* runtime CALL resolver loads; only main programs become verbs.  Skip
* comments in every style (* ! REM // and /* */ blocks) when finding
* the first real statement.
FIRST = ""
INBLK = 0
NA = DCOUNT(SRC, @AM)
FOR I = 1 TO NA
   LN = TRIM(SRC<I>)
   IF INBLK THEN
      P = INDEX(LN, "*/", 1)
      IF P > 0 THEN
         INBLK = 0
         LN = TRIM(LN[P + 2, LEN(LN)])
      END ELSE
         LN = ""
      END
   END
   IF LN # "" THEN
      C2 = LN[1, 2]
      BEGIN CASE
      CASE C2 = "/*"
         IF INDEX(LN, "*/", 1) = 0 THEN INBLK = 1
      CASE LN[1, 1] = "*" OR LN[1, 1] = "!" OR C2 = "//"
         X = 0
      CASE OCONV(FIELD(LN, " ", 1), "MCU") = "REM"
         X = 0
      CASE 1
         FIRST = LN
         I = NA
      END CASE
   END
NEXT I
IF FIRST[1, 11] = "SUBROUTINE " OR FIRST = "SUBROUTINE" THEN
   X = CREATEFILE("LIB", "DIR")
   RC = COMPILE("shared", FN:"/":IT, "LIB/":IT)
   IF RC = 0 THEN
      PRINT "[244] ":IT:" cataloged as a subroutine"
   END ELSE
      PRINT "[247] compilation of ":IT:" failed"
   END
   STOP
END
RC = COMPILE("exe", FN:"/":IT, "CATALOG/":IT)
IF RC = 0 ELSE
   PRINT "[247] compilation of ":IT:" failed"
   STOP
END
OPEN "VOC" TO V ELSE
   PRINT "cannot open VOC"
   STOP
END
WRITE "V":@AM:"CATALOG/":IT ON V, IT
PRINT "[244] ":IT:" cataloged"
