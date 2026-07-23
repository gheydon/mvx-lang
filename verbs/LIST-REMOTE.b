/**
 * @file LIST-REMOTE
 * @version 1.0
 */
* LIST-REMOTE — the account's remote file bindings.
OPEN "." TO ACC ELSE
   PRINT "cannot open the account directory"
   STOP
END
READ RL FROM ACC, "REMOTE" ELSE RL = ""
N = DCOUNT(RL, @AM)
IF N = 0 THEN
   D = ENV("MVXDAEMON")
   IF D = "" THEN
      PRINT "no remote bindings (all files local)"
   END ELSE
      PRINT "no REMOTE record; $MVXDAEMON binds every LMDB file: ":D
   END
   STOP
END
FOR I = 1 TO N
   FN = FIELD(RL<I>, " ", 1)
   AD = FIELD(RL<I>, " ", 2)
   IF AD = "" THEN AD = "(default: ":ENV("MVXDAEMON"):")"
   PRINT FMT(FN, "L#20"):" ":AD
NEXT I
PRINT N:" binding(s)"
