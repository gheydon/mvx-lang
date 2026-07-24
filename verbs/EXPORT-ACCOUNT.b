* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
/**
 * @file EXPORT-ACCOUNT
 * @version 1.0
 */
* EXPORT-ACCOUNT — snapshot the whole account into legible directory
* files that git can track: the VOC, every dictionary, and the record
* data of reference-sized files.  The inverse of CONVERT-ACCOUNT, so a
* live hash-file account round-trips through git.  Files larger than
* the record threshold export their dictionary only, so bulk data (a
* million orders) never lands in git — CONVERT recreates them empty and
* they reload from their own source.  Threshold: $MVXEXPORT_MAX
* (default 5000).  A FILES manifest records each file's backend so
* CONVERT can rebuild it with the same driver.
OPEN "." TO ACC ELSE
   PRINT "cannot open the account directory"
   STOP
END
MAXREC = 5000
IF ENV("MVXEXPORT_MAX") # "" THEN MAXREC = ENV("MVXEXPORT_MAX") + 0
GOSUB 2000                              ;* ensure the .mvx descriptor
FILES = ""
NDATA = 0
NDICT = 0
NREC = 0
L = FILELIST()
NF = DCOUNT(L, @AM)
FOR I = 1 TO NF
   NM = L<I, 1>
   TY = L<I, 2>
   SKIP = 0
   * source and compiled-verb directories are not data files
   IF NM = "BP" OR NM = "CATALOG" OR NM = "LIB" THEN SKIP = 1
   * a NAME.DICT or NAME.EXP directory is already an exported artefact
   IF LEN(NM) >= 5 THEN
      IF NM[LEN(NM) - 4, 5] = ".DICT" THEN SKIP = 1
   END
   IF LEN(NM) >= 4 THEN
      IF NM[LEN(NM) - 3, 4] = ".EXP" THEN SKIP = 1
   END
   IF SKIP = 0 THEN GOSUB 1000
NEXT I
WRITE FILES ON ACC, "FILES"
PRINT NDATA:" file(s), ":NDICT:" dictionary(ies), ":NREC:" record(s) exported"
STOP

* ---- 1000: export one file NM (type TY) to legible directory form -----
1000
TYOUT = "lmdb"
IF TY = "D" THEN TYOUT = "dir"
IF TY # "D" AND TY # "L" THEN TYOUT = TY      ;* a bound driver name
* dictionary -> NM.DICT, created only when it actually holds records
OPEN "DICT", NM TO DSRC THEN
   DMADE = 0
   SELECT DSRC
   DDONE = 0
   LOOP
      READNEXT DID ELSE DDONE = 1
   UNTIL DDONE DO
      READ DR FROM DSRC, DID THEN
         IF DMADE = 0 THEN
            X = CREATEFILE(NM:".DICT", "DIR")
            OPEN NM:".DICT" TO DDST ELSE RETURN
            DMADE = 1
            NDICT = NDICT + 1
         END
         WRITE DR ON DDST, DID
      END
   REPEAT
END
* data -> NM.  Directory files are already legible on disk; hash files
* export their records unless the file is bulk (over the threshold).
IF TY # "D" THEN
   OPEN NM TO SRC THEN
      CNT = 0
      BIG = 0
      SELECT SRC
      CDONE = 0
      LOOP
         READNEXT XID ELSE CDONE = 1
      UNTIL CDONE DO
         CNT = CNT + 1
         IF CNT > MAXREC THEN
            BIG = 1
            CDONE = 1
         END
      REPEAT
      IF BIG THEN
         PRINT "  ":NM:": over ":MAXREC:" records — dictionary only"
      END ELSE
         X = CREATEFILE(NM, "DIR")
         OPEN NM TO DST ELSE RETURN
         SELECT SRC
         WDONE = 0
         LOOP
            READNEXT ID ELSE WDONE = 1
         UNTIL WDONE DO
            READ R FROM SRC, ID THEN
               WRITE R ON DST, ID
               NREC = NREC + 1
            END
         REPEAT
      END
   END
END
NDATA = NDATA + 1
FILES<-1> = NM:" ":TYOUT
RETURN

* ---- 2000: ensure the .mvx account descriptor exists ------------------
2000
IF OSREAD(".mvx") = "" THEN
   AP = ENV("MVXACCTPATH")
   ANM = AP
   FOR AK = 1 TO LEN(AP)
      IF AP[AK, 1] = "/" THEN ANM = AP[AK + 1, LEN(AP)]
   NEXT AK
   Y = OSWRITE("# MVX account descriptor":CHAR(10):"name = ":ANM:CHAR(10):"version = 1":CHAR(10), ".mvx")
END
RETURN
