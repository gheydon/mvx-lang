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
