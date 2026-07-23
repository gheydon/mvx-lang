* COMMON shared between main and subroutine
COMMON TOTAL, ARR(3)
COMMON /NAMED/ TAG
TOTAL = 5
ARR(2) = "two"
TAG = "main"
CALL CSUB
PRINT TOTAL:" ":ARR(2):" ":ARR(3):" ":TAG
END
