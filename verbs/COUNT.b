* COUNT {DICT} file
S = TRIM(SENTENCE())
NAME = FIELD(S, " ", 2)
DICTF = 0
IF NAME = "DICT" THEN
   DICTF = 1
   NAME = FIELD(S, " ", 3)
END
IF NAME = "" THEN
   PRINT "usage: COUNT {DICT} filename"
   STOP
END
IF DICTF THEN
   OPEN "DICT", NAME TO F ELSE
      PRINT "cannot open DICT ":NAME
      STOP
   END
END ELSE
   OPEN NAME TO F ELSE
      PRINT "cannot open ":NAME
      STOP
   END
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
