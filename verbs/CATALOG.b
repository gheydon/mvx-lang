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
* runtime CALL resolver loads; only main programs become verbs.
FIRST = ""
NA = DCOUNT(SRC, @AM)
FOR I = 1 TO NA
   LN = TRIM(SRC<I>)
   IF LN # "" THEN
      IF LN[1, 1] # "*" AND LN[1, 1] # "!" THEN
         FIRST = LN
         GOTO 100
      END
   END
NEXT I
100 IF FIRST[1, 11] = "SUBROUTINE " OR FIRST = "SUBROUTINE" THEN
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
