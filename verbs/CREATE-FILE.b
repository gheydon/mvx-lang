* CREATE-FILE name {DIR}
S = SENTENCE()
NAME = FIELD(S, " ", 2)
TYPE = FIELD(S, " ", 3)
IF NAME = "" THEN
   PRINT "usage: CREATE-FILE filename {DIR}"
   STOP
END
IF TYPE = "DIR" THEN
   OK = CREATEFILE(NAME, "DIR")
END ELSE
   OK = CREATEFILE(NAME)
END
IF OK THEN
   PRINT "[417] file ":NAME:" created"
END ELSE
   PRINT "unable to create ":NAME:" (does it already exist?)"
END
