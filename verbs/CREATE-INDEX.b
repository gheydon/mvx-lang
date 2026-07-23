* /**
*  * @file CREATE-INDEX
*  * @version 1.0
*  */
* CREATE-INDEX file item — index a dictionary attribute item.
* Only D-type attribute items are indexable: a computed item whose
* value depends on anything outside the record would go silently
* stale (the TRANS() rule, ARCHITECTURE.md 5.4).
S = TRIM(SENTENCE())
FN = FIELD(S, " ", 2)
IT = FIELD(S, " ", 3)
IF FN = "" OR IT = "" THEN
   PRINT "usage: CREATE-INDEX file item"
   STOP
END
OPEN FN TO F ELSE
   PRINT "cannot open ":FN
   STOP
END
OPEN "DICT", FN TO DC ELSE
   PRINT FN:" has no dictionary - nothing to index by"
   STOP
END
READ DI FROM DC, IT ELSE
   PRINT IT:" is not a dictionary item in ":FN
   STOP
END
IF DI<1>[1, 1] # "D" THEN
   PRINT IT:" is not a D-type attribute item - only local attribute"
   PRINT "extractions are indexable (the TRANS() rule)"
   STOP
END
IF NUM(DI<2>) = 0 OR DI<2> < 1 THEN
   PRINT IT:" has no usable attribute number"
   STOP
END
READ XL FROM DC, "%INDEXES%" ELSE XL = ""
LOCATE(IT, XL; POS) ELSE
   XL<-1> = IT
   WRITE XL ON DC, "%INDEXES%"
END
N = INDEXBUILD(F, IT)
IF N < 0 THEN
   PRINT "index build failed (backend without index capability?)"
END ELSE
   PRINT "index ":FN:".":IT:" built, ":N:" record(s)"
END
