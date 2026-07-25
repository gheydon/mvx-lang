* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* LIST-MAPS — the account's files with a declared relational mapping
* (a %MAP% control record), and the fields each maps.
L = FILELIST()
N = DCOUNT(L, @AM)
NM = 0
FOR I = 1 TO N
   FN = FIELD(L<I>, @VM, 1)
   OPEN "DICT", FN TO D THEN
      READ MP FROM D, "%MAP%" THEN
         READ MODE FROM D, "%MAPMODE%" ELSE MODE = "mirror"
         FLDS = ""
         NF = DCOUNT(MP, @AM)
         FOR J = 1 TO NF
            IF FLDS # "" THEN FLDS := " "
            FLDS := FIELD(MP<J>, @VM, 1)
         NEXT J
         PRINT FMT(FN, "L#16"):" ":FMT(MODE, "L#8"):" ":FLDS
         NM = NM + 1
      END
   END
NEXT I
IF NM = 0 THEN
   PRINT "no mappings defined"
END ELSE
   PRINT NM:" mapping(s)"
END
