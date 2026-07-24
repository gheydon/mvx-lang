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
*  * @file CLEAR-FILE
*  * @version 1.0
*  */
* CLEAR-FILE file — delete every record in the file's data section.
FN = FIELD(TRIM(SENTENCE()), " ", 2)
IF FN = "" THEN
   PRINT "usage: CLEAR-FILE filename"
   STOP
END
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
SELECT F
N = 0
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   DELETE F, ID
   N = N + 1
REPEAT
PRINT "[420] ":FN:" cleared (":N:" record(s))"
