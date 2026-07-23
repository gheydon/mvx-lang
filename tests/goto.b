* Labels, GOTO / GO TO, GOSUB / RETURN
X = 1
GOTO 100
PRINT "skipped"
100 PRINT "at 100, X=":X
IF X < 3 THEN
   X = X + 1
   GO TO 100
END
GOSUB 500
GOSUB 500
PRINT "after gosubs, Y=":Y
GOSUB 600
PRINT "done"
STOP
PRINT "unreachable"
500 Y = Y + 10
RETURN
600 PRINT "in 600"
GOSUB 500
PRINT "nested back, Y=":Y
RETURN
