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
 * @file CONVERT-ACCOUNT
 * @version 1.0
 */
* CONVERT-ACCOUNT — turn a freshly cloned account (legible directory
* files) into a working account backed by hash files.  The inverse of
* EXPORT-ACCOUNT: the VOC and every reference file's records are moved
* into LMDB (or the driver named in the FILES manifest), their
* directory form is removed so the hash file becomes live, and then
* BUILD finishes the job — bulk data files are created empty from their
* dictionaries, BP source is cataloged, and packages are linked.  A
* file the manifest marks "dir" is left as a directory file on purpose.
* Run in the clone with developer privilege:
*    scripts/mvx-convert.sh <account>   (or: mvx-tcl -a . -c CONVERT-ACCOUNT)
OPEN "." TO ACC ELSE
   PRINT "cannot open the account directory"
   STOP
END
READ FILETYPES FROM ACC, "FILES" ELSE FILETYPES = ""
* Collect the directory files to migrate first: migrating mutates the
* file list, so it is not safe to migrate while iterating FILELIST().
NAMES = ""
L = FILELIST()
NF = DCOUNT(L, @AM)
FOR I = 1 TO NF
   NM = L<I, 1>
   TY = L<I, 2>
   SKIP = 0
   IF TY # "D" THEN SKIP = 1                 ;* already a hash file
   IF NM = "BP" OR NM = "CATALOG" OR NM = "LIB" THEN SKIP = 1
   IF LEN(NM) >= 5 THEN
      IF NM[LEN(NM) - 4, 5] = ".DICT" THEN SKIP = 1
   END
   IF LEN(NM) >= 4 THEN
      IF NM[LEN(NM) - 3, 4] = ".EXP" THEN SKIP = 1
   END
   IF SKIP = 0 THEN NAMES<-1> = NM
NEXT I
NMIG = 0
NREC = 0
NN = DCOUNT(NAMES, @AM)
FOR I = 1 TO NN
   NM = NAMES<I>
   GOSUB 1000
NEXT I
PRINT NMIG:" file(s) migrated to hash files, ":NREC:" record(s) loaded"
* BUILD provisions the remainder from the git-tracked configuration:
* empty data files from lone dictionaries, cataloged BP, linked
* packages, rebuilt indexes, and the .mvx descriptor.
EXECUTE "BUILD"
STOP

* ---- 1000: migrate directory file NM into a hash file -----------------
1000
* target backend from the FILES manifest; "dir" means keep it legible
TYPE = ""
FOR K = 1 TO DCOUNT(FILETYPES, @AM)
   IF FIELD(FILETYPES<K>, " ", 1) = NM THEN TYPE = FIELD(FILETYPES<K>, " ", 2)
NEXT K
IF TYPE = "dir" THEN RETURN
IF TYPE = "" THEN TYPE = "lmdb"
* Stage the records into a temporary hash file (a straight OPEN of NM
* still resolves to the directory, which shadows any same-named hash
* file), then drop the directory and recreate NM as the hash file.
TMP = "%CVT.":NM:"%"
X = DELETEFILE(TMP)
GOSUB 1500                               ;* create TMP with backend TYPE (target=TMP)
OPEN NM TO SRC ELSE RETURN
OPEN TMP TO TDST ELSE RETURN
SELECT SRC
C = 0
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   READ R FROM SRC, ID THEN
      WRITE R ON TDST, ID
      C = C + 1
   END
REPEAT
SRC = ""
X = DELETEFILE(NM)                       ;* remove the directory form
TGT = NM
GOSUB 1600                               ;* create NM with backend TYPE (target=TGT)
OPEN NM TO FIN ELSE RETURN
OPEN TMP TO TSRC ELSE RETURN
SELECT TSRC
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   READ R FROM TSRC, ID THEN WRITE R ON FIN, ID
REPEAT
X = DELETEFILE(TMP)
* import the dictionary (still a directory file, NM.DICT) into the hash
OPEN NM:".DICT" TO DDSRC THEN
   OPEN "DICT", NM TO DDST THEN
      SELECT DDSRC
      DONE = 0
      LOOP
         READNEXT DID ELSE DONE = 1
      UNTIL DONE DO
         READ DR FROM DDSRC, DID THEN WRITE DR ON DDST, DID
      REPEAT
   END
END
NMIG = NMIG + 1
NREC = NREC + C
RETURN

* ---- 1500: create the temp file TMP with backend TYPE -----------------
1500
IF TYPE = "lmdb" THEN
   X = CREATEFILE(TMP)
END ELSE
   X = CREATEFILE(TMP, "USING ":TYPE)
END
RETURN

* ---- 1600: create the final file NM with backend TYPE -----------------
1600
IF TYPE = "lmdb" THEN
   X = CREATEFILE(NM)
END ELSE
   X = CREATEFILE(NM, "USING ":TYPE)
END
RETURN
