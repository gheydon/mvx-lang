/**
 * @file keyin
 * KEYIN() decoding: printable, named specials, escape sequences,
 * pushback after a lone ESC, and @() screen-code strings.
 */
ECHO OFF
ECHO ON
LOOP
   K = KEYIN()
UNTIL K = "q" DO
   PRINT "key: ":K
REPEAT
PRINT "at codes: ":LEN(@(-1)):" ":LEN(@(-4)):" ":LEN(@(5, 10)):" ":SEQ(@(0, 0))
PRINT "bye"
END
