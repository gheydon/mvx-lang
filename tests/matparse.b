* MATPARSE / MATBUILD: in-memory array <-> delimited string, no file.
DIM A(3)
* default delimiter is @FM: last element absorbs overflow
MATPARSE A FROM "one":@FM:"two":@FM:"three":@FM:"four"
PRINT "p1=":A(1)
PRINT "p2=":A(2)
PRINT "p3=":A(3)              ;* three <FM> four
* short input leaves trailing elements empty
MATPARSE A FROM "solo"
PRINT "s1=[":A(1):"]"
PRINT "s2=[":A(2):"]"
* build back with @FM, trailing empties stripped
A(1) = "x"
A(2) = "y"
A(3) = ""
MATBUILD R FROM A
PRINT "built fields=":DCOUNT(R, @FM)
* custom delimiter via USING
DIM P(4)
MATPARSE P FROM "a,b,c,d" USING ","
PRINT "csv=":P(1):P(2):P(3):P(4)
MATBUILD S FROM P USING "-"
PRINT "joined=":S
* trailing empties dropped with a custom delimiter too
DIM Q(4)
Q(1) = "m"
Q(2) = "n"
Q(3) = ""
Q(4) = ""
MATBUILD T FROM Q USING "|"
PRINT "trim=[":T:"]"
