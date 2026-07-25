* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* CREATE-MAP file field {field...} — declare the file's relational
* mapping and build it.  Writes the %MAP% control record into the
* dictionary, so from now on every WRITE mirrors the record into the
* projection (mirror mode).  BUILD-MAP re-runs the backfill on demand.
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
NT = DCOUNT(S, " ")
IF FN = "" OR NT < 3 THEN
   PRINT "usage: CREATE-MAP file field {field...}"
   STOP
END
OPEN "DICT", FN TO DD ELSE
   PRINT "cannot open DICT ":FN
   STOP
END
SPEC = ""
NS = 0
FOR I = 3 TO NT
   FLD = FIELD(S, " ", I)
   READ DI FROM DD, FLD THEN
      IF DI<1>[1, 1] = "D" THEN
         CONV = DI<3>
         C2 = CONV[1, 2]
         T = "TEXT"
         IF C2 = "MD" OR C2 = "MR" OR C2 = "ML" THEN T = "NUMERIC"
         IF C2 = "MT" THEN T = "TIME"
         IF CONV[1, 1] = "D" THEN T = "DATE"
         NS = NS + 1
         SPEC<NS> = FLD:@VM:DI<2>:@VM:CONV:@VM:T:@VM:DI<6>
      END ELSE
         PRINT FLD:" is not a mappable dictionary item (skipped)"
      END
   END ELSE
      PRINT FLD:" is not a dictionary item (skipped)"
   END
NEXT I
IF NS = 0 THEN
   PRINT "no mappable fields"
   STOP
END
WRITE SPEC ON DD, "%MAP%"
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
RC = MAPBUILD(F, SPEC)
BEGIN CASE
CASE RC = -2
   PRINT "backend does not support mapping"
CASE RC < 0
   PRINT "map build failed"
CASE 1
   PRINT "mapped ":RC:" record(s); writes are now mirrored"
END CASE
