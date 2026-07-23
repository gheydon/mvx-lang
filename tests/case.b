* BEGIN CASE / CASE / END CASE
FOR I = 1 TO 4
   BEGIN CASE
   CASE I = 1
      PRINT "one"
   CASE I = 2
      PRINT "two"
   CASE 1
      PRINT "other ":I
   END CASE
NEXT I
END
