/**
 * @file mouse
 * Basic mouse support (#57): @(-9)/@(-10) toggle click reporting, KEYIN()
 * decodes an SGR mouse report to the token "MOUSE", and MOUSE() returns the
 * last click as col : @VM : row : @VM : button : @VM : event.
 */
ECHO OFF
ECHO ON
PRINT "toggles: ":LEN(@(-9)):" ":LEN(@(-10))
LOOP
   K = KEYIN()
UNTIL K = "q" DO
   IF K = "MOUSE" THEN
      M = MOUSE()
      PRINT "mouse: ":M<1,1>:",":M<1,2>:" ":M<1,3>:" ":M<1,4>
   END ELSE
      PRINT "key: ":K
   END
REPEAT
PRINT "bye"
END
