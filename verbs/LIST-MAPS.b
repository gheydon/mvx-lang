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
* (a %MAP% control record): the mapping mode, whether it is still current
* with the dictionary or stale, and the fields it maps.  A mapping is a
* snapshot of the dictionary taken at CREATE-MAP time, so an edit to a
* mapped dict item (conversion, attribute, association, or its removal)
* leaves the mapping "stale" until DELETE-MAP + CREATE-MAP rebuild it.
L = FILELIST()
N = DCOUNT(L, @AM)
NM = 0
FOR I = 1 TO N
   FN = FIELD(L<I>, @VM, 1)
   OPEN "DICT", FN TO D THEN
      READ MP FROM D, "%MAP%" THEN
         READ MODE FROM D, "%MAPMODE%" ELSE MODE = "mirror"
         FLDS = ""
         STALE = 0
         NF = DCOUNT(MP, @AM)
         FOR J = 1 TO NF
            MF = FIELD(MP<J>, @VM, 1)
            IF FLDS # "" THEN FLDS := " "
            FLDS := MF
            * compare the snapshot against the live dict item
            AT = FIELD(MP<J>, @VM, 2)
            CV = FIELD(MP<J>, @VM, 3)
            TY = FIELD(MP<J>, @VM, 4)
            AS = FIELD(MP<J>, @VM, 5)
            READ DI FROM D, MF THEN
               IF DI<1>[1, 1] # "D" THEN
                  STALE = 1
               END ELSE
                  NCV = DI<3>
                  C2 = NCV[1, 2]
                  NTY = "TEXT"
                  IF C2 = "MD" OR C2 = "MR" OR C2 = "ML" THEN NTY = "NUMERIC"
                  IF C2 = "MT" THEN NTY = "TIME"
                  IF NCV[1, 1] = "D" THEN NTY = "DATE"
                  IF DI<2> # AT THEN STALE = 1
                  IF NCV # CV THEN STALE = 1
                  IF NTY # TY THEN STALE = 1
                  IF DI<6> # AS THEN STALE = 1
               END
            END ELSE
               STALE = 1
            END
         NEXT J
         STATE = "current"
         IF STALE THEN STATE = "stale"
         PRINT FMT(FN, "L#16"):" ":FMT(MODE, "L#8"):" ":FMT(STATE, "L#8"):" ":FLDS
         NM = NM + 1
      END
   END
NEXT I
IF NM = 0 THEN
   PRINT "no mappings defined"
END ELSE
   PRINT NM:" mapping(s)"
END
