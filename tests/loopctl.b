* CONTINUE (next iteration) and EXIT (leave loop) in FOR and LOOP.
* numeric FOR: skip 3, stop at 6
S = ""
FOR I = 1 TO 10
   IF I = 3 THEN CONTINUE
   IF I = 6 THEN EXIT
   S = S:I:" "
NEXT I
PRINT "fornum=":S

* string/step FOR over a dynamic array of values
LINE = "a":@VM:"skip":@VM:"b":@VM:"stop":@VM:"c"
OUT = ""
N = DCOUNT(LINE, @VM)
FOR K = 1 TO N
   V = LINE<1,K>
   IF V = "skip" THEN CONTINUE
   IF V = "stop" THEN EXIT
   OUT = OUT:V
NEXT K
PRINT "forval=":OUT

* LOOP / REPEAT with EXIT and CONTINUE
T = 0
C = 0
LOOP
   C = C + 1
   IF C > 100 THEN EXIT       ;* safety
   IF MOD(C, 2) = 0 THEN CONTINUE
   T = T + C
   IF C >= 9 THEN EXIT
REPEAT
PRINT "loopsum=":T:" last=":C

* nested loops: EXIT leaves only the inner one
G = ""
FOR A = 1 TO 3
   FOR B = 1 TO 3
      IF B = 2 THEN EXIT
      G = G:A:B:" "
   NEXT B
NEXT A
PRINT "nested=":G

* single-line LOOP WHILE / UNTIL cond DO (classic Pick/R83 form)
W = 0 ; SW = ""
LOOP WHILE W < 4 DO
   W = W + 1
   SW = SW:W:" "
REPEAT
PRINT "loopwhile=":SW
U = 0 ; SU = ""
LOOP UNTIL U >= 3 DO
   U = U + 1
   SU = SU:U:" "
REPEAT
PRINT "loopuntil=":SU
