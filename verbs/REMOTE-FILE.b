/**
 * @file REMOTE-FILE
 * @version 1.0
 */
* REMOTE-FILE file {daemon} — bind a file to a daemon.  The REMOTE
* record in the account directory lists bindings, one per line:
* "SPEC {addr}" ("*" binds every LMDB file).  Without an addr the
* default $MVXDAEMON applies.  Binding affects resolution only:
* existing local data does not move.
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
AD = FIELD(S, " ", 3)
IF FN = "" THEN
   PRINT "usage: REMOTE-FILE file {daemon-address}"
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
      PRINT FN:" is already bound remote"
      STOP
   END
NEXT I
E = FN
IF AD # "" THEN E = FN:" ":AD
RL<-1> = E
WRITE RL ON ACC, "REMOTE"
PRINT FN:" bound remote":
IF AD # "" THEN
   PRINT " (":AD:")"
END ELSE
   PRINT " (default daemon)"
END
PRINT "note: existing local data does not move automatically"
