* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* /**
*  * @file LINK-PKG
*  * @version 2.0
*  */
* LINK-PKG path — link a package and its dependency closure.
* The PKG manifest (attr 1 name, 2 version, 3 description, 4..
* dependency names) drives resolution: a dependency is satisfied by an
* already-linked package of that name, or found beside the requiring
* package, or on $MVXPKGPATH — and is linked automatically.
S = TRIM(SENTENCE())
P = FIELD(S, " ", 2)
IF P = "" THEN
   PRINT "usage: LINK-PKG /path/to/package"
   STOP
END
OPEN "." TO ACC ELSE
   PRINT "cannot open the account directory"
   STOP
END
READ PKGS FROM ACC, "PACKAGES" ELSE PKGS = ""

* names of already-linked packages
LNAMES = ""
NP = DCOUNT(PKGS, @AM)
FOR LI = 1 TO NP
   CUR = PKGS<LI>
   GOSUB 9000
   LNAMES<LI> = PNAME
NEXT LI

QUEUE = ""
QUEUE<1> = P
ADDED = ""
LOOP
UNTIL QUEUE = "" DO
   CUR = QUEUE<1>
   QUEUE = DELETE(QUEUE, 1, 0, 0)
   SKIP = 0
   LOCATE(CUR, PKGS; POS) THEN SKIP = 1
   IF SKIP = 0 THEN
      * a package is identified by its PKG manifest, not its VOC: a
      * package may expose only subroutines (a LIB library) and carry
      * no verb records at all.
      OPEN CUR TO PV ELSE
         PRINT CUR:" is not a package (cannot open ":CUR:")"
         STOP
      END
      READ PKGREC FROM PV, "PKG" ELSE
         PRINT CUR:" is not a package (no PKG manifest)"
         STOP
      END
      GOSUB 9000
      * the manifest's systems field (attr 4) lists the MV platforms the
      * package targets; refuse one that declares systems but not this one.
      IF PSYS # "" THEN
         OKSYS = 0
         NSY = DCOUNT(PSYS, " ")
         FOR SI = 1 TO NSY
            IF FIELD(PSYS, " ", SI) = "mvx" THEN OKSYS = 1
         NEXT SI
         IF OKSYS = 0 THEN
            PRINT CUR:" does not support mvx (systems: ":PSYS:")"
            STOP
         END
      END
      LOCATE(PNAME, LNAMES; POS) THEN SKIP = 1
   END
   IF SKIP = 0 THEN
      PKGS<-1> = CUR
      LNAMES<-1> = PNAME
      ADDED<-1> = CUR
      DEPLIST = PDEPS
      ND = DCOUNT(DEPLIST, @AM)
      FOR DI = 1 TO ND
         D = DEPLIST<DI>
         LOCATE(D, LNAMES; POS) THEN
            X = 0
         END ELSE
            GOSUB 9200
            IF RPATH = "" THEN
               PRINT "cannot resolve dependency '":D:"' of ":CUR
               PRINT "searched: linked packages, ":CUR:"/../, $MVXPKGPATH"
               STOP
            END
            QUEUE<-1> = RPATH
         END
      NEXT DI
   END
REPEAT
IF ADDED = "" THEN
   PRINT P:" is already linked"
   STOP
END
WRITE PKGS ON ACC, "PACKAGES"
NA = DCOUNT(ADDED, @AM)
FOR LI = 1 TO NA
   PRINT "linked ":ADDED<LI>
NEXT LI
STOP

* ---- 9000: manifest of CUR -> PNAME, PVER, PDEPS -----------------------
9000
PNAME = ""
PVER = ""
PSYS = ""
PDEPS = ""
OPEN CUR TO MPD THEN
   READ MF FROM MPD, "PKG" THEN
      PNAME = MF<1>
      PVER = MF<2>
      PSYS = MF<4>
      MN = DCOUNT(MF, @AM)
      FOR MI = 5 TO MN
         IF MF<MI> # "" THEN
            PDEPS<-1> = MF<MI>
         END
      NEXT MI
   END
END
IF PNAME = "" THEN
   LS = 0
   FOR MI = 1 TO LEN(CUR)
      IF CUR[MI, 1] = "/" THEN LS = MI
   NEXT MI
   PNAME = CUR[LS + 1, LEN(CUR)]
END
RETURN

* ---- 9200: resolve dependency name D near CUR -> RPATH -----------------
9200
RPATH = ""
LS = 0
FOR MI = 1 TO LEN(CUR)
   IF CUR[MI, 1] = "/" THEN LS = MI
NEXT MI
CAND = CUR[1, LS]:D
OPEN CAND:"/VOC" TO TV THEN
   RPATH = CAND
   RETURN
END
PP = ENV("MVXPKGPATH")
NSEG = DCOUNT(PP, ":")
FOR MI = 1 TO NSEG
   SEG = FIELD(PP, ":", MI)
   IF SEG # "" THEN
      CAND = SEG:"/":D
      OPEN CAND:"/VOC" TO TV THEN
         RPATH = CAND
         RETURN
      END
   END
NEXT MI
RETURN
