* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* LIST-CONNECTIONS — the named connection profiles, with secret fields
* (token, password) masked.  Non-secret fields (driver, address,
* namespace) are shown so you can see where a connection points.
L = LISTCONN()
N = DCOUNT(L, @AM)
IF L = "" THEN
   PRINT "no connections defined"
   STOP
END
FOR I = 1 TO N
   PRINT L<I>
NEXT I
