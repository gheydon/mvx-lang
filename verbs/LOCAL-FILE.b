/**
 * @file LOCAL-FILE
 * @version 1.0
 */
* LOCAL-FILE file — remove a file's remote binding.
FN = FIELD(TRIM(SENTENCE()), " ", 2)
IF FN = "" THEN
   PRINT "usage: LOCAL-FILE file"
   STOP
END
OPEN "." TO ACC ELSE
   PRINT "cannot open the account directory"
   STOP
END
READ RL FROM ACC, "REMOTE" ELSE RL = ""
N = DCOUNT(RL, @AM)
FOR I = 1 TO N
   IF FIELD(RL<I>, " ", 1) = FN THEN
      RL = DELETE(RL, I, 0, 0)
      WRITE RL ON ACC, "REMOTE"
      PRINT FN:" bound local"
      STOP
   END
NEXT I
PRINT FN:" is not bound remote"
