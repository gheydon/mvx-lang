* /**
*  * @file LIST-PKGS
*  * @version 2.0
*  */
* LIST-PKGS — linked packages with manifest name@version and deps.
OPEN "." TO ACC ELSE
   PRINT "cannot open the account directory"
   STOP
END
READ PKGS FROM ACC, "PACKAGES" ELSE PKGS = ""
NP = DCOUNT(PKGS, @AM)
IF NP = 0 THEN
   PRINT "no packages linked"
   STOP
END
FOR LI = 1 TO NP
   CUR = PKGS<LI>
   GOSUB 9000
   STATE = "broken"
   OPEN CUR:"/VOC" TO PV THEN STATE = "ok"
   DEPTXT = ""
   ND = DCOUNT(PDEPS, @AM)
   FOR DI = 1 TO ND
      IF DEPTXT = "" THEN
         DEPTXT = "requires ":PDEPS<DI>
      END ELSE
         DEPTXT = DEPTXT:", ":PDEPS<DI>
      END
   NEXT DI
   PRINT FMT(PNAME:"@":PVER, "L#14"):" ":FMT(STATE, "L#6"):" ":FMT(CUR, "L#46"):" ":DEPTXT
NEXT LI
PRINT NP:" package(s) linked"
STOP

* ---- 9000: manifest of CUR -> PNAME, PVER, PDEPS -----------------------
9000
PNAME = ""
PVER = ""
PDEPS = ""
OPEN CUR TO MPD THEN
   READ MF FROM MPD, "PKG" THEN
      PNAME = MF<1>
      PVER = MF<2>
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
IF PVER = "" THEN PVER = "?"
RETURN
