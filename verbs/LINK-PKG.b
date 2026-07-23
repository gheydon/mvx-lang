* /**
*  * @file LINK-PKG
*  * @version 1.0
*  */
* LINK-PKG path — link a package into this account.  The PACKAGES
* record in the account directory lists linked packages, one per
* attribute; TCL adds their VOCs to verb resolution.
S = TRIM(SENTENCE())
P = FIELD(S, " ", 2)
IF P = "" THEN
   PRINT "usage: LINK-PKG /path/to/package"
   STOP
END
OPEN P:"/VOC" TO PV ELSE
   PRINT P:" is not a package (no VOC directory)"
   STOP
END
OPEN "." TO ACC ELSE
   PRINT "cannot open the account directory"
   STOP
END
READ PKGS FROM ACC, "PACKAGES" ELSE PKGS = ""
LOCATE(P, PKGS; POS) THEN
   PRINT P:" is already linked"
   STOP
END
PKGS<-1> = P
WRITE PKGS ON ACC, "PACKAGES"
PRINT "linked ":P
