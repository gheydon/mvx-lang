* CREATE-FILE name {DIR | USING <driver> {connection}}
* The file's backend is decided at creation: a directory file, a
* local LMDB file (the default), or a file on another driver
* (lmdbnet, and later postgres/mongo) bound in the account's BINDINGS
* record. For lmdbnet the connection defaults to $MVXDAEMON.
S = TRIM(SENTENCE())
NAME = FIELD(S, " ", 2)
TYPE = OCONV(FIELD(S, " ", 3), "MCU")
IF NAME = "" THEN
   PRINT "usage: CREATE-FILE name {DIR | USING driver {connection}}"
   STOP
END
BEGIN CASE
CASE TYPE = "DIR"
   OK = CREATEFILE(NAME, "DIR")
CASE TYPE = "USING"
   DRV = FIELD(S, " ", 4)
   CONN = FIELD(S, " ", 5, 99)
   IF DRV = "" THEN
      PRINT "usage: CREATE-FILE name USING driver {connection}"
      STOP
   END
   TV = "USING ":DRV
   IF CONN # "" THEN TV = TV:" ":CONN
   OK = CREATEFILE(NAME, TV)
CASE TYPE = ""
   OK = CREATEFILE(NAME)
CASE 1
   PRINT "usage: CREATE-FILE name {DIR | USING driver {connection}}"
   STOP
END CASE
IF OK THEN
   PRINT "[417] file ":NAME:" created"
END ELSE
   PRINT "unable to create ":NAME:" (does it already exist?)"
END
