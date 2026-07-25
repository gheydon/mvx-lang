* SUM reduces the lowest delimiter level
A = 10:@VM:20:@VM:30
PRINT "sumvm=":SUM(A)
* mixed levels: sum @VM within each @FM group
B = "1":@VM:"2":@FM:"3":@VM:"4":@VM:"5"
R = SUM(B)
PRINT "sumfm1=":R<1>
PRINT "sumfm2=":R<2>
* subvalue level
C = "1":@SM:"2":@VM:"3":@SM:"4"
Q = SUM(C)
PRINT "sumsm=":Q<1,1>:",":Q<1,2>
* MAXIMUM / MINIMUM across all levels
D = "5":@VM:"2":@FM:"9":@VM:"1"
PRINT "max=":MAXIMUM(D):" min=":MINIMUM(D)
PRINT "nonum=[":MAXIMUM("abc"):"]"
* SUM ignores non-numeric
PRINT "sumign=":SUM("3":@VM:"x":@VM:"4")
