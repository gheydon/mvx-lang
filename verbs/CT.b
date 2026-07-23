* CT file id — copy record to terminal, classic numbered format
S = SENTENCE()
NAME = FIELD(S, " ", 2)
ID = FIELD(S, " ", 3)
IF NAME = "" OR ID = "" THEN
   PRINT "usage: CT filename id"
   STOP
END
OPEN NAME TO F ELSE
   PRINT "cannot open ":NAME
   STOP
END
READ R FROM F, ID THEN
   PRINT ID
   FOR I = 1 TO DCOUNT(R, @AM)
      PRINT FMT(I, "R%3"):" ":R<I>
   NEXT I
END ELSE
   PRINT ID:" not on file"
END
