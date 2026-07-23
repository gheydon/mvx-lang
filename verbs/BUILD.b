/**
 * @file BUILD
 * @version 1.0
 */
* BUILD — provision this account from its git-tracked configuration.
* A cloned account carries its schema and source but not its data
* files (those live in the store, kept out of git).  BUILD:
*   1. ensures the account (VOC) exists;
*   2. creates each data file whose dictionary is present but whose data
*      file is missing — the type comes from the FILES manifest, then a
*      per-file prompt (interactive) or the lmdb default;
*   3. imports each tracked dictionary into the new file;
*   4. catalogs BP source into runnable verbs;
*   5. links the packages listed in PACKAGES.
* Run it in a freshly cloned directory: mvx-tcl -a . -c BUILD (developer
* privilege, for cataloging).
JUNK = CREATEFILE("VOC")
* the .mvx descriptor marks this as an account (the VOC is not a
* physical file when it lives in LMDB or on a daemon)
IF OSREAD(".mvx") = "" THEN
   AP = ENV("MVXACCTPATH")
   ANM = AP
   FOR AK = 1 TO LEN(AP)
      IF AP[AK, 1] = "/" THEN ANM = AP[AK + 1, LEN(AP)]
   NEXT AK
   Y = OSWRITE("# MVX account descriptor":CHAR(10):"name = ":ANM:CHAR(10):"version = 1":CHAR(10), ".mvx")
END
OPEN "." TO ACC ELSE
   PRINT "cannot open the account directory"
   STOP
END
READ FILETYPES FROM ACC, "FILES" ELSE FILETYPES = ""
ASK = 0
IF ENV("MVXBUILD_ASK") # "" THEN ASK = 1

* ---- 1/2/3: data files from tracked dictionaries ----------------------
* A dictionary tracked for git is a directory file "<name>.DICT".
NMADE = 0
L = FILELIST()
NF = DCOUNT(L, @AM)
FOR I = 1 TO NF
   ENTRY = L<I>
   NM = ENTRY<1, 1>
   * dictionaries appear as directory files ending in .DICT
   IF LEN(NM) > 5 THEN
      IF NM[LEN(NM) - 4, 5] = ".DICT" THEN
         BASE = NM[1, LEN(NM) - 5]
         GOSUB 1000
      END
   END
NEXT I

* ---- 4: catalog BP source ---------------------------------------------
NCAT = 0
JUNK = CREATEFILE("CATALOG", "DIR")     ;* the compiled-verb directory
OPEN "BP" TO BP THEN
   SELECT BP
   DONE = 0
   LOOP
      READNEXT ID ELSE DONE = 1
   UNTIL DONE DO
      IF ID[1, 1] # "." THEN
         EXECUTE "CATALOG BP ":ID CAPTURING R
         NCAT = NCAT + 1
      END
   REPEAT
END

* ---- 5: link packages -------------------------------------------------
READ PKGS FROM ACC, "PACKAGES" ELSE PKGS = ""
NP = DCOUNT(PKGS, @AM)
FOR I = 1 TO NP
   EXECUTE "LINK-PKG ":PKGS<I> CAPTURING R
NEXT I

PRINT "built account: ":NMADE:" file(s) created, ":NCAT:" verb(s) cataloged, ":NP:" package(s) linked"
STOP

* ---- 1000: ensure data file BASE exists, import its dictionary ---------
1000
OPEN BASE TO EXIST THEN RETURN     ;* data file already present
* type precedence: FILES manifest override, then the %FILE% hint
* stored in the dictionary, then a prompt (interactive), then lmdb.
TY = ""
FOR K = 1 TO DCOUNT(FILETYPES, @AM)
   IF FIELD(FILETYPES<K>, " ", 1) = BASE THEN
      TY = FIELD(FILETYPES<K>, " ", 2)
   END
NEXT K
CONN = ""
IF TY = "" THEN
   OPEN BASE:".DICT" TO MDD THEN
      READ META FROM MDD, "%FILE%" THEN
         TY = META<1, 2>
         CONN = META<1, 3>
      END
   END
END
IF TY = "" AND ASK THEN
   PRINT "file ":BASE:" — type? [lmdb/dir] (default lmdb): ":
   INPUT TY
   TY = TRIM(TY)
END
IF TY = "" THEN TY = "lmdb"
BEGIN CASE
CASE TY = "dir"
   OK = CREATEFILE(BASE, "DIR")
CASE TY = "lmdb"
   OK = CREATEFILE(BASE)
CASE CONN # ""
   OK = CREATEFILE(BASE, "USING ":TY:" ":CONN)
CASE 1
   OK = CREATEFILE(BASE, "USING ":TY)
END CASE
IF OK = 0 THEN
   PRINT "could not create ":BASE
   RETURN
END
NMADE = NMADE + 1
* import the tracked dictionary (directory file BASE.DICT) into DICT.BASE
OPEN BASE:".DICT" TO DSRC ELSE RETURN
OPEN "DICT", BASE TO DDST ELSE RETURN
SELECT DSRC
DDONE = 0
LOOP
   READNEXT DID ELSE DDONE = 1
UNTIL DDONE DO
   READ DR FROM DSRC, DID THEN WRITE DR ON DDST, DID
REPEAT
* rebuild indexes declared in the dictionary's %INDEXES% control record
READ IXL FROM DDST, "%INDEXES%" ELSE IXL = ""
FOR IX = 1 TO DCOUNT(IXL, @AM)
   EXECUTE "CREATE-INDEX ":BASE:" ":IXL<IX> CAPTURING R
NEXT IX
RETURN
