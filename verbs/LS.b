* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* LS {dir} — list a directory's entries, like the shell `ls` other MV
* systems provide.  It opens the directory through the directory driver and
* SELECTs it, so it needs no OS-execution privilege and behaves the same on
* every backend (no shell-out).  With no argument it lists the account.
S = TRIM(SENTENCE())
D = TRIM(FIELD(S, " ", 2))
IF D = "" THEN D = "."
OPEN D TO LF ELSE
   PRINT "LS: cannot open ":D
   STOP
END
SELECT LF
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   PRINT ID
REPEAT
