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
*  * @file UNLINK-PKG
*  * @version 2.0
*  */
* UNLINK-PKG path — remove a linked package, unless another linked
* package declares it as a dependency.
S = TRIM(SENTENCE())
P = FIELD(S, " ", 2)
IF P = "" THEN
   PRINT "usage: UNLINK-PKG /path/to/package"
   STOP
END
OPEN "." TO ACC ELSE
   PRINT "cannot open the account directory"
   STOP
END
READ PKGS FROM ACC, "PACKAGES" ELSE PKGS = ""
LOCATE(P, PKGS; TPOS) ELSE
   PRINT P:" is not linked"
   STOP
END
CUR = P
GOSUB 9000
TARGET = PNAME
NP = DCOUNT(PKGS, @AM)
FOR LI = 1 TO NP
   IF LI # TPOS THEN
      CUR = PKGS<LI>
      GOSUB 9000
      LOCATE(TARGET, PDEPS; DP) THEN
         PRINT "cannot unlink ":TARGET:": ":PKGS<LI>:" depends on it"
         STOP
      END
   END
NEXT LI
PKGS = DELETE(PKGS, TPOS, 0, 0)
WRITE PKGS ON ACC, "PACKAGES"
PRINT "unlinked ":P
STOP

* ---- 9000: manifest of CUR -> PNAME, PDEPS -----------------------------
9000
PNAME = ""
PDEPS = ""
OPEN CUR TO MPD THEN
   READ MF FROM MPD, "PKG" THEN
      PNAME = MF<1>
      MN = DCOUNT(MF, @AM)
      FOR MI = 5 TO MN
         DNAME = MF<MI>
         * a '?' prefix marks an optional dependency (see LINK-PKG); strip it
         * so the reverse-dependency check still recognises the name — an
         * optional dependency is still a dependency for unlink safety.
         IF DNAME[1, 1] = "?" THEN DNAME = DNAME[2, LEN(DNAME)]
         IF DNAME # "" THEN
            PDEPS<-1> = DNAME
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
