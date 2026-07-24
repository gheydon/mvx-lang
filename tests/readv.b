* READV / WRITEV: read and write a single attribute of a record.
OPEN "PARTS" TO F ELSE
   IF CREATEFILE("PARTS") ELSE PRINT "create failed" ; STOP
   OPEN "PARTS" TO F ELSE PRINT "cannot open" ; STOP
END
WRITE "Widget":@AM:"9.99":@AM:"RED" ON F, "W1"
* read individual attributes
READV NAME FROM F, "W1", 1 THEN PRINT "name=":NAME ELSE PRINT "no W1"
READV COST FROM F, "W1", 2 THEN PRINT "cost=":COST
READV COLOR FROM F, "W1", 3 THEN PRINT "color=":COLOR
* reading past the end yields empty
READV GONE FROM F, "W1", 9 THEN PRINT "attr9=[":GONE:"]"
* missing record takes ELSE
READV X FROM F, "NOPE", 1 THEN PRINT "phantom" ELSE PRINT "NOPE absent ok"
* WRITEV replaces one attribute, preserving the rest
WRITEV "12.00" ON F, "W1", 2
READ R FROM F, "W1" THEN PRINT "after=[":R:"]"
* WRITEV past the end extends the record with field marks
WRITEV "BOX" ON F, "W1", 5
READ R FROM F, "W1" THEN PRINT "attrs=":DCOUNT(R, @AM)
READV FIVE FROM F, "W1", 5 THEN PRINT "attr5=":FIVE
* WRITEV to a brand-new record creates it
WRITEV "solo" ON F, "NEW", 1
READ R FROM F, "NEW" THEN PRINT "new=[":R:"]" ELSE PRINT "NEW missing"
* locked read then single-attribute write keeps the record intact
READU R FROM F, "W1" THEN
   WRITEV "green" ON F, "W1", 3
END ELSE PRINT "W1 lock missing"
READV C FROM F, "W1", 3 THEN PRINT "recolor=":C
READV N FROM F, "W1", 1 THEN PRINT "still=":N
IF DELETEFILE("PARTS") ELSE PRINT "deletefile failed"
