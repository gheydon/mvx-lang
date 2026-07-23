* /**
*  * @file UNLINK-PKG
*  * @version 1.0
*  */
* UNLINK-PKG path — remove a linked package from this account.
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
LOCATE(P, PKGS; POS) THEN
   PKGS = DELETE(PKGS, POS, 0, 0)
   WRITE PKGS ON ACC, "PACKAGES"
   PRINT "unlinked ":P
END ELSE
   PRINT P:" is not linked"
END
