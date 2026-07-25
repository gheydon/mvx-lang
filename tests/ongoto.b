FOR N = 1 TO 4
   ON N GOTO 100, 200, 300
   PRINT N:": fell through (out of range)"
   GOTO 400
100 PRINT N:": one"
   GOTO 400
200 PRINT N:": two"
   GOTO 400
300 PRINT N:": three"
400 NULL
NEXT N
* ON GOSUB
FOR M = 1 TO 3
   ON M GOSUB 500, 600
   PRINT "after gosub ":M
NEXT M
STOP
500 PRINT "  sub-A"
   RETURN
600 PRINT "  sub-B"
   RETURN
