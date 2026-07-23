/**
 * @file IMPORT
 * @version 1.0
 */
* IMPORT file {dir} — mirror a directory file back into a hash file.
* Every record in the directory is written to the file, and records in
* the file that are absent from the directory are deleted — so a git
* checkout, merge, or pull that added, changed, or removed record
* files is reflected exactly.  Default source: <file>.EXP.
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
DIR = FIELD(S, " ", 3)
IF FN = "" THEN
   PRINT "usage: IMPORT file {directory}"
   STOP
END
IF DIR = "" THEN DIR = FN:".EXP"
OPEN DIR TO SRC ELSE
   PRINT "cannot open export directory ":DIR
   STOP
END
OPEN FN TO DST ELSE
   PRINT "cannot open ":FN
   STOP
END
* write every exported record; remember which ids we have seen
SEEN = ""
SELECT SRC
NW = 0
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   READ R FROM SRC, ID THEN
      WRITE R ON DST, ID
      SEEN<-1> = ID
      NW = NW + 1
   END
REPEAT
* delete file records no longer present in the directory
SELECT DST
ND = 0
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   LOCATE(ID, SEEN; POS) ELSE
      DELETE DST, ID
      ND = ND + 1
   END
REPEAT
PRINT NW:" record(s) imported, ":ND:" removed"
