/**
 * @file smoke
 * C-style comments are an MVX extension: block and // line comments,
 * usable without the classic * prefix.
 */
* Smoke test: subset coverage
// line comment style
A = 5 /* inline block comment */ + 0
B = 2.5
C = A * B + 1
PRINT "C=":C
IF C > 10 THEN
   PRINT "big"
END ELSE
   PRINT "small"
END
TOTAL = 0
FOR I = 1 TO 10
   TOTAL = TOTAL + I
NEXT I
PRINT "TOTAL=":TOTAL
N = 0
LOOP
WHILE N < 3 DO
   PRINT "N=":N
   N = N + 1
REPEAT
DIM ARR(5)
FOR I = 1 TO 5
   ARR(I) = I * I
NEXT I
PRINT "ARR(4)=":ARR(4)
S = "HELLO"
S = S:" ":"WORLD"
PRINT S
IF S = "HELLO WORLD" THEN PRINT "concat ok" ELSE PRINT "concat BAD"
PRINT "neg=":-A
PRINT "mod=":MOD(17, 5)
PRINT "int=":INT(7.9)
PRINT "sqrt=":SQRT(81)
PRINT "A=":A, "B=":B
END
