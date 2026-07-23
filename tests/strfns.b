* String intrinsics, MAT, INPUT
PRINT CHAR(65):CHAR(66)
PRINT SEQ("A")
PRINT STR("ab", 3)
PRINT SPACE(3):"|"
PRINT TRIM("  hello   world  ")
S = "aa,bb,cc,dd"
PRINT FIELD(S, ",", 2)
PRINT FIELD(S, ",", 2, 2)
PRINT INDEX(S, ",", 2)
PRINT NUM("123"):" ":NUM("12A"):" ":NUM("")
DIM M(3)
MAT M = 7
PRINT M(1):" ":M(3)
DIM B1(2)
DIM B2(2)
B1(1) = "x"
B1(2) = "y"
MAT B2 = MAT B1
PRINT B2(2)
INPUT NAME
PRINT "hello ":NAME
END
