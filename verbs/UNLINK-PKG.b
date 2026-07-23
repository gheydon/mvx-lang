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
      FOR MI = 4 TO MN
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
