* $INCLUDE / $INSERT: splice shared source in, sharing macro state.
$INCLUDE inc_defs.b
PRINT "true=":TRUE
R = "a":AM:"b":AM:"c"
PRINT "fields=":DCOUNT(R, AM)
* a $DEFINE inside the include is visible here
$IFDEF FEATURE_X
PRINT "feature x enabled"
$ENDIF
$IFNDEF FEATURE_X
PRINT "should not print"
$ENDIF
* $INSERT is a synonym; the equate it declares is usable after
$INSERT inc_msg.b
PRINT MSG
