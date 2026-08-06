* STOP <code> — an optional numeric argument sets the process exit status, so a
* verb (e.g. CHECK) can gate CI.  Bare STOP stays exit 0; here a taken branch
* stops with 4, and the line after is never reached.
   PRINT "start"
   IF 1 THEN STOP 4
   PRINT "unreached"
