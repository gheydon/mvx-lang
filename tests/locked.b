* LOCKED clause parses and, with no contention (a local file has no
* cross-session lock authority), behaves like a plain READU: the read
* takes the THEN branch when found and ELSE when absent.
OPEN "PARTS" TO F ELSE
   IF CREATEFILE("PARTS") ELSE PRINT "create failed" ; STOP
   OPEN "PARTS" TO F ELSE PRINT "cannot open" ; STOP
END
WRITE "Widget":@AM:"9.99":@AM:"RED" ON F, "W1"
* block-form LOCKED before THEN/ELSE
READU R FROM F, "W1" LOCKED
   PRINT "W1 busy"
END THEN
   PRINT "got ":R<1>
END ELSE
   PRINT "W1 gone"
END
WRITE R ON F, "W1"
* single-line LOCKED clause
READU R FROM F, "NOPE" LOCKED PRINT "busy" THEN PRINT "phantom" ELSE PRINT "absent ok"
* READVU with a LOCKED clause
READVU C FROM F, "W1", 3 LOCKED
   PRINT "attr busy"
END THEN PRINT "color=":C
WRITEV C ON F, "W1", 3
* MATREADU with a LOCKED clause
DIM A(3)
MATREADU A FROM F, "W1" LOCKED
   PRINT "mat busy"
END THEN
   PRINT "mat name=":A(1)
END ELSE PRINT "mat gone"
MATWRITE A ON F, "W1"
IF DELETEFILE("PARTS") ELSE PRINT "deletefile failed"
