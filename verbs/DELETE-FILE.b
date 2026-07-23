* DELETE-FILE name
NAME = FIELD(SENTENCE(), " ", 2)
IF NAME = "" THEN
   PRINT "usage: DELETE-FILE filename"
   STOP
END
IF DELETEFILE(NAME) THEN
   PRINT "[418] file ":NAME:" deleted"
END ELSE
   PRINT "unable to delete ":NAME:" (does it exist?)"
END
