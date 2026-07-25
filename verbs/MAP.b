* MVX — a native compiler and runtime for Pick/MultiValue BASIC.
* Copyright (C) 2026 Gordon Heydon.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as
* published by the Free Software Foundation.  There is NO WARRANTY, to
* the extent permitted by law; see the LICENSE file for details.
*
* SPDX-License-Identifier: GPL-2.0-only
* MAP file {items...|ALL} {DATA} — the relational schema the dictionary
* implies.  Map only the named dictionary items (the usual case), or ALL
* / * for every item.  Single-valued attributes become parent-table
* columns; each association (dict attribute 6) becomes a child table
* keyed (id, seq).  Column types come from the conversion code.  DATA
* also previews the projected rows.
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
IF FN = "" THEN
   PRINT "usage: MAP file {items...|ALL} {DATA}"
   STOP
END
* parse the item selection and the DATA flag from the remaining tokens
NT = DCOUNT(S, " ")
MODE = ""
SELS = ""
FOR AI = 3 TO NT
   T = FIELD(S, " ", AI)
   IF T = "DATA" THEN
      MODE = "DATA"
   END ELSE
      SELS<-1> = T
   END
NEXT AI
* ALL when nothing is named, or ALL / * is given
ALLMAP = 0
NS = DCOUNT(SELS, @AM)
IF NS = 0 THEN ALLMAP = 1
IF SELS<1> = "ALL" OR SELS<1> = "*" THEN ALLMAP = 1
OPEN "DICT", FN TO D ELSE
   PRINT "cannot open DICT ":FN
   STOP
END
* ---- collect D-items into parallel arrays --------------------------------
NM = ""
AN = ""
TY = ""
AS = ""
N = 0
SELECT D
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   KEEP = ALLMAP
   IF KEEP = 0 THEN
      FOR SI = 1 TO NS
         IF SELS<SI> = ID THEN KEEP = 1
      NEXT SI
   END
   IF ID[1, 1] # "%" AND KEEP THEN
      READ DI FROM D, ID THEN
         IF DI<1>[1, 1] = "D" THEN
            CONV = DI<3>
            T = "TEXT"
            IF CONV[1, 2] = "MD" OR CONV[1, 2] = "MR" OR CONV[1, 2] = "ML" THEN
               T = "NUMERIC"
            END
            IF CONV[1, 2] = "MT" THEN
               T = "TIME"
            END ELSE
               IF CONV[1, 1] = "D" THEN T = "DATE"
            END
            * indexed assignment (not <-1>): an empty association value
            * must still occupy a slot, keeping the arrays aligned
            N = N + 1
            NM<N> = ID
            AN<N> = DI<2>
            TY<N> = T
            AS<N> = DI<6>
         END
      END
   END
REPEAT
IF N = 0 THEN
   PRINT "no mappable dictionary items in ":FN
   STOP
END
* ---- parent table --------------------------------------------------------
PRINT "CREATE TABLE ":FN:" ("
PRINT "    id TEXT PRIMARY KEY"
FOR I = 1 TO N
   IF AS<I> = "" THEN
      PRINT "  , ":NM<I>:" ":TY<I>
   END
NEXT I
PRINT ");"
* ---- one child table per association (first occurrence emits it) ---------
FOR I = 1 TO N
   A = AS<I>
   IF A # "" THEN
      FIRST = 1
      FOR J = 1 TO I - 1
         IF AS<J> = A THEN FIRST = 0
      NEXT J
      IF FIRST THEN
         PRINT ""
         PRINT "CREATE TABLE ":FN:"_":A:" ("
         PRINT "    id TEXT"
         PRINT "  , seq INT"
         FOR J = 1 TO N
            IF AS<J> = A THEN
               PRINT "  , ":NM<J>:" ":TY<J>
            END
         NEXT J
         PRINT "  , PRIMARY KEY (id, seq)"
         PRINT ");"
      END
   END
NEXT I
IF MODE # "DATA" THEN STOP
* ---- projected rows ------------------------------------------------------
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
PRINT ""
PRINT "-- projected rows --"
SELECT F
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   READ R FROM F, ID ELSE R = ""
   ROW = FN:": id=":ID
   FOR I = 1 TO N
      IF AS<I> = "" THEN
         ROW = ROW:" ":NM<I>:"=":R<AN<I>>
      END
   NEXT I
   PRINT ROW
   * child rows: one per value position of each association
   FOR I = 1 TO N
      A = AS<I>
      IF A # "" THEN
         FIRST = 1
         FOR J = 1 TO I - 1
            IF AS<J> = A THEN FIRST = 0
         NEXT J
         IF FIRST THEN
            NV = 1
            FOR J = 1 TO N
               IF AS<J> = A THEN
                  VC = DCOUNT(R<AN<J>>, @VM)
                  IF VC > NV THEN NV = VC
               END
            NEXT J
            FOR SEQ = 1 TO NV
               CROW = FN:"_":A:": id=":ID:" seq=":SEQ
               FOR J = 1 TO N
                  IF AS<J> = A THEN
                     CROW = CROW:" ":NM<J>:"=":FIELD(R<AN<J>>, @VM, SEQ)
                  END
               NEXT J
               PRINT CROW
            NEXT SEQ
         END
      END
   NEXT I
REPEAT
