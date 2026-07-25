* MATCHES pattern operator, MATCHFIELD, and the NULL statement.
* Character classes: N numeric, A alphabetic, X any; count 0 = any run.
IF "12345" MATCHES "5N" THEN PRINT "5N yes" ELSE PRINT "5N no"
IF "12345" MATCHES "4N" THEN PRINT "4N wrong" ELSE PRINT "4N no ok"
IF "ABC" MATCHES "3A" THEN PRINT "3A yes"
IF "AB12" MATCHES "2A2N" THEN PRINT "2A2N yes"
IF "anything!" MATCHES "0X" THEN PRINT "0X yes"
IF "" MATCHES "0N" THEN PRINT "empty 0N yes"
* literals in the pattern (quoted and bare)
IF "01-234" MATCHES "2N'-'3N" THEN PRINT "phone yes" ELSE PRINT "phone no"
IF "A100Z" MATCHES "1A0N1A" THEN PRINT "wrap yes"
* alternatives via value marks: numeric OR alphabetic
P = "3N":@VM:"3A"
IF "abc" MATCHES P THEN PRINT "alt-a yes"
IF "123" MATCHES P THEN PRINT "alt-n yes"
IF "1a2" MATCHES P THEN PRINT "alt bad" ELSE PRINT "alt no ok"
* MATCH is a synonym; result usable as a value
V = "42" MATCH "2N"
PRINT "value=":V
* MATCHFIELD pulls out the n-th component
PRINT "mf1=":MATCHFIELD("01-234", "2N'-'3N", 1)
PRINT "mf2=":MATCHFIELD("01-234", "2N'-'3N", 2)
PRINT "mf3=":MATCHFIELD("01-234", "2N'-'3N", 3)
PRINT "mf-nomatch=[":MATCHFIELD("xx", "2N", 1):"]"
* NULL statement is a no-op (e.g. an empty THEN branch)
IF "x" MATCHES "1A" THEN NULL ELSE PRINT "unreachable"
PRINT "done"
