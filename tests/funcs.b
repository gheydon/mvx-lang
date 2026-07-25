* Caller: DEFFUN declares each function so NAME(...) is a call.
DEFFUN SQUARE(1)
DEFFUN FACT(1)
DEFFUN GREET(1)
PRINT "square=":SQUARE(6)
PRINT "pyth=":SQUARE(3) + SQUARE(4)
PRINT "nest=":SQUARE(SQUARE(2))
PRINT "fact6=":FACT(6)
PRINT GREET("world")
PRINT "empty=[":GREET(""):"]"
* used in a condition and a loop
T = 0
FOR I = 1 TO 4
   T += SQUARE(I)
NEXT I
PRINT "sumsq=":T
