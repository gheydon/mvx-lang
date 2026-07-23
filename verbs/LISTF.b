* /**
*  * @file LISTF
*  * @version 1.0
*  */
* LISTF — the MV files in this account.
L = FILELIST()
N = DCOUNT(L, @AM)
PRINT FMT("File", "L#24"):" Type"
FOR I = 1 TO N
   E = L<I>
   NM = E<1, 1>
   TY = E<1, 2>
   IF TY = "D" THEN
      TYX = "directory"
   END ELSE
      TYX = "lmdb"
   END
   PRINT FMT(NM, "L#24"):" ":TYX
NEXT I
PRINT N:" file(s)"
