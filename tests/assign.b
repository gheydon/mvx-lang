* Compound assignment: target OP= value  ->  target = target OP value.
X = 5
X += 3
PRINT "add=":X
X -= 2
PRINT "sub=":X
X *= 4
PRINT "mul=":X
X /= 3
PRINT "div=":X
* := appends (string concatenation)
S = "a"
S := "b"
S := "c":"d"
PRINT "cat=":S
* works on array elements and dynamic-array positions
DIM A(3)
A(2) = "x"
A(2) := "y"
PRINT "elem=":A(2)
R = "1":@VM:"2":@VM:"3"
R<1,2> += 10
PRINT "pos=":R<1,2>
* accumulate in a loop
T = 0
FOR I = 1 TO 5
   T += I
NEXT I
PRINT "sum=":T
