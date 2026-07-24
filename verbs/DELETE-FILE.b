* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* DELETE-FILE name
NAME = FIELD(SENTENCE(), " ", 2)
IF NAME = "" THEN
   PRINT "usage: DELETE-FILE filename"
   STOP
END
IF DELETEFILE(NAME) THEN
   PRINT "[418] file ":NAME:" deleted"
END ELSE
   PRINT "unable to delete ":NAME:" (does it exist?)"
END
