* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* LIST-CREDENTIALS — the account credential store, with secret values
* masked.  The raw secrets never leave the runtime.
L = LISTCRED()
N = DCOUNT(L, @AM)
IF L = "" THEN
   PRINT "no credentials stored"
   STOP
END
PRINT "driver     target             key        fields"
FOR I = 1 TO N
   PRINT L<I>
NEXT I
PRINT N:" credential(s)"
