* /**
*  * @file LIST-INDEXES
*  * @version 1.0
*  */
* LIST-INDEXES file
FN = FIELD(TRIM(SENTENCE()), " ", 2)
IF FN = "" THEN
   PRINT "usage: LIST-INDEXES file"
   STOP
END
OPEN "DICT", FN TO DC ELSE
   PRINT FN:" has no dictionary"
   STOP
END
READ XL FROM DC, "%INDEXES%" ELSE XL = ""
N = DCOUNT(XL, @AM)
IF N = 0 THEN
   PRINT "no indexes on ":FN
   STOP
END
FOR I = 1 TO N
   PRINT XL<I>
NEXT I
PRINT N:" index(es)"
