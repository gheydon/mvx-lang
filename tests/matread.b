* MATREAD / MATWRITE: dimensioned array <-> record.
* Fields (@FM) map to elements in storage order; the last element
* absorbs any overflow fields; MATWRITE drops trailing empty elements.
OPEN "PARTS" TO F ELSE
   IF CREATEFILE("PARTS") ELSE PRINT "create failed" ; STOP
   OPEN "PARTS" TO F ELSE PRINT "cannot open" ; STOP
END
* seed a 4-field record, read it into a 3-element array
WRITE "Widget":@AM:"9.99":@AM:"RED":@AM:"EXTRA" ON F, "W1"
DIM A(3)
MATREAD A FROM F, "W1" THEN
   PRINT "name=":A(1)
   PRINT "price=":A(2)
   PRINT "tail=":A(3)          ;* absorbs RED and EXTRA
END ELSE PRINT "W1 missing"
* short record: trailing elements come back empty
WRITE "Solo" ON F, "W2"
MATREAD A FROM F, "W2" THEN
   PRINT "short1=[":A(1):"]"
   PRINT "short2=[":A(2):"]"
   PRINT "short3=[":A(3):"]"
END ELSE PRINT "W2 missing"
* missing record takes ELSE
MATREAD A FROM F, "NOPE" THEN PRINT "phantom" ELSE PRINT "NOPE absent ok"
* MATWRITE: build a record; trailing empty element is trimmed
A(1) = "One"
A(2) = "Two"
A(3) = ""
MATWRITE A ON F, "W3"
READ R FROM F, "W3" THEN PRINT "built=[":R:"]" ELSE PRINT "W3 missing"
PRINT "fields=":DCOUNT(R, @AM)
* round-trip through a locked read
MATREADU A FROM F, "W1" THEN
   A(2) = "12.00"
   MATWRITE A ON F, "W1"
END ELSE PRINT "W1 lock missing"
MATREAD A FROM F, "W1" THEN PRINT "updated price=":A(2)
IF DELETEFILE("PARTS") ELSE PRINT "deletefile failed"
