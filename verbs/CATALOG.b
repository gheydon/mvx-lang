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
