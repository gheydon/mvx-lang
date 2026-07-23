* Storage: LMDB named-DB file
OPEN "PARTS" TO F ELSE PRINT "cannot open PARTS" ; STOP
WRITE "Widget":@AM:"9.99" ON F, "W1"
WRITE "Gadget":@AM:"4.50" ON F, "W2"
WRITE "Sprocket":@AM:"1.25" ON F, "W3"
READ R FROM F, "W1" THEN PRINT R<1>:" costs ":R<2> ELSE PRINT "W1 missing"
READ R FROM F, "NOPE" THEN PRINT "phantom" ELSE PRINT "NOPE absent ok"
READU R FROM F, "W2" THEN
   R<2> = "5.00"
   WRITE R ON F, "W2"
END ELSE PRINT "W2 missing"
READ R FROM F, "W2" THEN PRINT "W2 now ":R<2>
DELETE F, "W3"
READ R FROM F, "W3" THEN PRINT "W3 undead" ELSE PRINT "W3 deleted ok"
SELECT F
N = 0
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   N = N + 1
   READ R FROM F, ID THEN PRINT ID:" = ":R<1>
REPEAT
PRINT "count ":N
* clean up for reruns
DELETE F, "W1"
DELETE F, "W2"
END
