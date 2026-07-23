* /**
*  * @file LIST-PKGS
*  * @version 1.0
*  */
* LIST-PKGS — show packages linked into this account.
OPEN "." TO ACC ELSE
   PRINT "cannot open the account directory"
   STOP
END
READ PKGS FROM ACC, "PACKAGES" ELSE PKGS = ""
N = DCOUNT(PKGS, @AM)
IF N = 0 THEN
   PRINT "no packages linked"
   STOP
END
FOR I = 1 TO N
   P = PKGS<I>
   STATE = "broken: no VOC"
   OPEN P:"/VOC" TO PV THEN STATE = "ok"
   PRINT FMT(P, "L#48"):" ":STATE
NEXT I
PRINT N:" package(s) linked"
