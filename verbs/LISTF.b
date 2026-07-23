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
   BEGIN CASE
   CASE TY = "D"
      TYX = "directory"
   CASE TY = "N"
      TYX = "remote"
   CASE 1
      TYX = "lmdb"
   END CASE
   PRINT FMT(NM, "L#24"):" ":TYX
NEXT I
PRINT N:" file(s)"
