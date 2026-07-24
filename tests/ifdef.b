* compile-time conditionals and defines
$DEFINE BONUS 5
$IFDEF MVX
PRINT "on mvx"
$ELSE
PRINT "elsewhere"
$ENDIF
$IFNDEF UNIDATA
PRINT "portable path"
$ENDIF
X = 10
$IFDEF MVX
X = X + BONUS
$ENDIF
PRINT X
