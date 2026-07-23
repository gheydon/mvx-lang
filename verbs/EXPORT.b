/**
 * @file EXPORT
 * @version 1.0
 */
* EXPORT file {dir} — copy a file's records into a directory file so
* git can track them.  A directory file stores one record per Unix
* file with attributes as lines (ARCHITECTURE.md 9.1), so the export
* is git-native with no sync logic: commit, branch, and merge the
* directory directly.  Default target: <file>.EXP.  Uses the active
* select list when one exists.
S = TRIM(SENTENCE())
A = FIELD(S, " ", 2)
ISDICT = 0
IF A = "DICT" THEN
   ISDICT = 1
   FN = FIELD(S, " ", 3)
   DIR = FIELD(S, " ", 4)
   IF DIR = "" THEN DIR = FN:".DICT"
END ELSE
   FN = A
   DIR = FIELD(S, " ", 3)
   IF DIR = "" THEN DIR = FN:".EXP"
END
IF FN = "" THEN
   PRINT "usage: EXPORT {DICT} file {directory}"
   STOP
END
IF ISDICT THEN
   OPEN "DICT", FN TO SRC ELSE
      PRINT "cannot open DICT ":FN
      STOP
   END
END ELSE
   OPEN FN TO SRC ELSE
      PRINT "cannot open ":FN
      STOP
   END
END
X = CREATEFILE(DIR, "DIR")
OPEN DIR TO DST ELSE
   PRINT "cannot open export directory ":DIR
   STOP
END
IF SYSTEM(11) = 0 THEN SELECT SRC
N = 0
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   READ R FROM SRC, ID THEN
      WRITE R ON DST, ID
      N = N + 1
   END
REPEAT
PRINT N:" record(s) exported to ":DIR
