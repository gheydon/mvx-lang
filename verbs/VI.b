* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
/**
 * @file VI
 * @version 1.0
 */
* VI file id — edit a record in an external editor.  The record is
* exported to a temp file with one attribute per line, the editor
* ($MVXEDITOR / $VISUAL / $EDITOR / vi) runs on it, and the result is
* imported back and written.  This is how hash-file records reach
* line-oriented tools; the same export/import shape underlies EXPORT.
* Needs unrestricted privilege (runs an external editor); ED is the
* tier-safe alternative.
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
ID = FIELD(S, " ", 3)
IF FN = "" OR ID = "" THEN
   PRINT "usage: VI file id"
   STOP
END
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
NEW = 0
READ REC FROM F, ID ELSE
   REC = ""
   NEW = 1
END
LF = CHAR(10)
TMP = TMPNAM()
* attribute marks become newlines on the way out
X = OSWRITE(CHANGE(REC, @AM, LF), TMP)
IF X = 0 THEN
   PRINT "cannot write temp file"
   STOP
END
RC = EDITFILE(TMP)
IF RC # 0 THEN
   Y = OSDELETE(TMP)
   PRINT "editor exited abnormally; ":ID:" unchanged"
   STOP
END
BODY = OSREAD(TMP)
Y = OSDELETE(TMP)
* strip a single trailing newline the editor may add, then map back
IF BODY[LEN(BODY), 1] = LF THEN BODY = BODY[1, LEN(BODY) - 1]
NEWREC = CHANGE(BODY, LF, @AM)
IF NEW AND NEWREC = "" THEN
   PRINT ID:" not created (empty)"
   STOP
END
IF NOT(NEW) AND NEWREC = REC THEN
   PRINT ID:" unchanged"
   STOP
END
WRITE NEWREC ON F, ID
PRINT "'":ID:"' filed"
