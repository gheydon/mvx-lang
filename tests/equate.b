* EQUATE: compile-time constants, expanded at each use site.
EQUATE TRUE TO 1
EQUATE FALSE TO 0
EQUATE AM TO CHAR(254)
EQUATE PI TO 3.14159
EQUATE GREETING TO "hello"
EQUATE SIZE TO 5
EQUATE AREA TO PI * 2 * 2          ;* expression, referencing another EQUATE
* constants in expressions
IF TRUE THEN PRINT "true works"
IF NOT(FALSE) THEN PRINT "false works"
PRINT "pi=":PI
PRINT "area=":AREA
PRINT GREETING:" world"
* CHAR(254) equate used as a mark
R = "a":AM:"b":AM:"c"
PRINT "fields=":DCOUNT(R, AM)
* EQUATE as a DIM size and loop bound (stays numeric / specialised)
DIM V(SIZE)
FOR I = 1 TO SIZE
   V(I) = I * I
NEXT I
PRINT "squares=":V(1):",":V(2):",":V(3):",":V(4):",":V(5)
* LITERALLY spelling
EQUATE MAX LITERALLY 100
PRINT "max=":MAX
* EQU short spelling
EQU VM TO CHAR(253)
PRINT "vm=":SEQ(VM)
