* /**
*  * @file DELETE
*  * @version 1.0
*  */
* DELETE file id {id ...} — delete records.
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
IF FN = "" OR FIELD(S, " ", 3) = "" THEN
   PRINT "usage: DELETE file id {id ...}"
   STOP
END
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
N = 0
NT = DCOUNT(S, " ")
FOR I = 3 TO NT
   ID = FIELD(S, " ", I)
   READ R FROM F, ID THEN
      DELETE F, ID
      N = N + 1
   END ELSE
      PRINT ID:" not on file"
   END
NEXT I
PRINT N:" record(s) deleted"
