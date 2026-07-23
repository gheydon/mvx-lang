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
   CASE TY = "L"
      TYX = "lmdb"
   CASE 1
      TYX = TY
   END CASE
   PRINT FMT(NM, "L#24"):" ":TYX
NEXT I
PRINT N:" file(s)"
