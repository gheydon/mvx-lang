* CREATE-FILE name {DIR | REMOTE {daemon-address}}
* The file's type is decided at creation: a directory file, a local
* LMDB file, or a daemon-backed file (bound in the account's REMOTE
* record; the address defaults to $MVXDAEMON).
S = TRIM(SENTENCE())
NAME = FIELD(S, " ", 2)
TYPE = FIELD(S, " ", 3)
ADDR = FIELD(S, " ", 4)
IF NAME = "" THEN
   PRINT "usage: CREATE-FILE filename {DIR | REMOTE {daemon-address}}"
   STOP
END
BEGIN CASE
CASE TYPE = "DIR"
   OK = CREATEFILE(NAME, "DIR")
CASE TYPE = "REMOTE"
   TV = "REMOTE"
   IF ADDR # "" THEN TV = "REMOTE ":ADDR
   OK = CREATEFILE(NAME, TV)
CASE TYPE = ""
   OK = CREATEFILE(NAME)
CASE 1
   PRINT "usage: CREATE-FILE filename {DIR | REMOTE {daemon-address}}"
   STOP
END CASE
IF OK THEN
   PRINT "[417] file ":NAME:" created"
END ELSE
   PRINT "unable to create ":NAME:" (does it already exist?)"
END
