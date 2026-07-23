* Dynamic-array operations
A = "red":@AM:"green":@AM:"blue"
PRINT A<2>
PRINT DCOUNT(A, @AM)
A<2> = "GREEN"
PRINT A<2>
A<-1> = "yellow"
PRINT DCOUNT(A, @AM):" ":A<4>
B = ""
B<1> = "x"
B<3> = "z"
PRINT DCOUNT(B, @AM)
V = "ruby":@VM:"crimson"
C = ""
C<1> = V
PRINT C<1,2>
C<1,2> = "scarlet"
PRINT C<1,2>
D = INSERT(A, 2, 0, 0, "orange")
PRINT D<2>:" ":DCOUNT(D, @AM)
E = DELETE(A, 1, 0, 0)
PRINT E<1>:" ":DCOUNT(E, @AM)
LOCATE("blue", A; POS) THEN PRINT "found at ":POS ELSE PRINT "not found"
LOCATE("mauve", A; POS) THEN PRINT "found" ELSE PRINT "insert at ":POS
LOCATE("green", V, 1; VP) THEN PRINT "vm miss" ELSE PRINT "vm insert ":VP
PRINT LEN(A<1>)
PRINT COUNT("banana", "an")
X = EXTRACT(A, 3)
PRINT X
IF A<2 THEN PRINT "compare parsed" ELSE PRINT "compare false"
END
