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
FN = FIELD(S, " ", 2)
DIR = FIELD(S, " ", 3)
IF FN = "" THEN
   PRINT "usage: EXPORT file {directory}"
   STOP
END
IF DIR = "" THEN DIR = FN:".EXP"
OPEN FN TO SRC ELSE
   PRINT "cannot open ":FN
   STOP
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
