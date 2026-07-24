* ON ERROR: a WRITE the backend rejects runs the clause instead of
* aborting.  The directory driver rejects a record id containing '/',
* which gives a deterministic backend failure.
OPEN "DIRDATA" TO D ELSE PRINT "cannot open DIRDATA" ; STOP
* a good write does not take the ON ERROR path
WRITE "kept" ON D, "GOOD" ON ERROR
   PRINT "unexpected error on GOOD"
END
READ R FROM D, "GOOD" THEN PRINT "GOOD=":R ELSE PRINT "GOOD missing"
* an invalid id triggers ON ERROR (block form)
WRITE "nope" ON D, "bad/id" ON ERROR
   PRINT "write error caught"
END
* single-line ON ERROR
WRITE "nope" ON D, "bad/id2" ON ERROR PRINT "single-line error caught"
* WRITEV honours ON ERROR too
WRITEV "x" ON D, "bad/id3", 1 ON ERROR
   PRINT "writev error caught"
END
* MATWRITE honours ON ERROR too
DIM A(2)
A(1) = "p"
A(2) = "q"
MATWRITE A ON D, "bad/id4" ON ERROR
   PRINT "matwrite error caught"
END
* ON ERROR coexists with THEN/ELSE on a read (inert: reads do not fault)
READ R FROM D, "GOOD" ON ERROR
   PRINT "read error"
END THEN PRINT "read ok" ELSE PRINT "read missing"
* ON ERROR + LOCKED + THEN/ELSE all on one READU
READU R FROM D, "GOOD" ON ERROR
   PRINT "ru error"
END LOCKED
   PRINT "ru locked"
END THEN PRINT "ru ok" ELSE PRINT "ru missing"
DELETE D, "GOOD"
