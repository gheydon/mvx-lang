* COUNT file
NAME = FIELD(SENTENCE(), " ", 2)
IF NAME = "" THEN
   PRINT "usage: COUNT filename"
   STOP
END
OPEN NAME TO F ELSE
   PRINT "cannot open ":NAME
   STOP
END
* use the active select list when one exists, classic style
IF SYSTEM(11) = 0 THEN SELECT F
N = 0
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   N = N + 1
REPEAT
PRINT N:" record(s) counted"
