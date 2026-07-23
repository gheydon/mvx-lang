* Storage: directory-backed file (attributes are lines on disk)
OPEN "DIRDATA" TO D ELSE PRINT "cannot open DIRDATA" ; STOP
WRITE "line one":@AM:"line two" ON D, "NOTE1"
READ R FROM D, "NOTE1" THEN
   PRINT DCOUNT(R, @AM):" attrs, second=":R<2>
END ELSE PRINT "NOTE1 missing"
SELECT D
READNEXT ID THEN PRINT "first id ":ID ELSE PRINT "empty"
DELETE D, "NOTE1"
READ R FROM D, "NOTE1" THEN PRINT "undead" ELSE PRINT "deleted ok"
END
