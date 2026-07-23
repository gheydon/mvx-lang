* /**
*  * @file CLEAR-FILE
*  * @version 1.0
*  */
* CLEAR-FILE file — delete every record in the file's data section.
FN = FIELD(TRIM(SENTENCE()), " ", 2)
IF FN = "" THEN
   PRINT "usage: CLEAR-FILE filename"
   STOP
END
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
SELECT F
N = 0
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   DELETE F, ID
   N = N + 1
REPEAT
PRINT "[420] ":FN:" cleared (":N:" record(s))"
