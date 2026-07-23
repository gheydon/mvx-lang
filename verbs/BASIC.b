* BASIC file item — compile a source record to an object.
* BASIC compiles; CATALOG links and publishes (ARCHITECTURE.md 6.4).
S = SENTENCE()
FN = FIELD(S, " ", 2)
IT = FIELD(S, " ", 3)
IF FN = "" OR IT = "" THEN
   PRINT "usage: BASIC filename itemname"
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
X = CREATEFILE(FN:".O", "DIR")
RC = COMPILE("c", FN:"/":IT, FN:".O/":IT:".o")
IF RC = 0 THEN
   PRINT "[241] ":IT:" compiled"
END ELSE
   PRINT "[247] compilation of ":IT:" failed"
END
