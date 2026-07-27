* Alphanumeric statement labels: NAME: definitions, GOSUB/GOTO/ON GOTO to
* names, a label preceding a statement on the same line, mixing with numeric
* labels, and a name used as both a label and a variable (separate namespaces).
GOSUB GREET
N = 2
ON N GOTO ONE, TWO, THREE
ONE: PRINT "one" ; GOTO DONE
TWO: PRINT "two" ; GOTO DONE
THREE: PRINT "three"
DONE:
GOTO 500
GREET:
   PRINT "hi from GREET"
   RETURN
500 PRINT "at numeric 500"
SAVE = 42
SAVE: PRINT "label SAVE, var SAVE=":SAVE
