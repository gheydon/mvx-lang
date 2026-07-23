* CALL / SUBROUTINE ABI test: by-reference args, expression args
X = 10
Y = 0
CALL ADDER(X, 32, Y)
PRINT "Y=":Y
CALL ADDER(Y, Y, Y)
PRINT "Y2=":Y
END
