* Dictionaries: DICT opens against both drivers
X = CREATEFILE("CUST")
OPEN "DICT", "CUST" TO DC ELSE PRINT "no lmdb dict" ; STOP
ITEM = "D":@AM:"1":@AM:"":@AM:"Customer Name":@AM:"20L":@AM:"S"
WRITE ITEM ON DC, "NAME"
READ D FROM DC, "NAME" THEN
   PRINT "NAME attr ":D<2>:" heading ":D<4>
END ELSE PRINT "dict item lost"
* dict and data are separate spaces
OPEN "CUST" TO CF ELSE PRINT "no data" ; STOP
WRITE "Ada" ON CF, "C1"
SELECT DC
N = 0
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   N = N + 1
REPEAT
PRINT "dict has ":N:" item(s)"
SELECT CF
READNEXT ID THEN PRINT "data id ":ID
* directory-backed file gets a dictionary too
Y = CREATEFILE("CUSTD", "DIR")
OPEN "DICT", "CUSTD" TO DD ELSE PRINT "no dir dict" ; STOP
WRITE "D":@AM:"2" ON DD, "CODE"
READ D FROM DD, "CODE" THEN PRINT "dir dict ok ":D<2> ELSE PRINT "bad"
* data select must not see the dictionary
OPEN "CUSTD" TO CD ELSE STOP
WRITE "rec" ON CD, "R1"
SELECT CD
M = 0
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   M = M + 1
REPEAT
PRINT "dir data ids ":M
* DICT of a nonexistent file takes ELSE
OPEN "DICT", "GHOST" TO GG THEN PRINT "phantom dict" ELSE PRINT "ghost else ok"
Z = DELETEFILE("CUST")
OPEN "DICT", "CUST" TO DC2 THEN PRINT "dict survived?!" ELSE PRINT "dict gone ok"
Z = DELETEFILE("CUSTD")
END
