/**
 * @file keyin
 * KEYIN() decoding: printable, named specials, escape sequences,
 * pushback after a lone ESC, SS3 application-cursor arrows,
 * and @() screen-code strings.
 */
ECHO OFF
ECHO ON
LOOP
   K = KEYIN()
UNTIL K = "q" DO
   PRINT "key: ":K
REPEAT
PRINT "at codes: ":LEN(@(-1)):" ":LEN(@(-4)):" ":LEN(@(5, 10)):" ":SEQ(@(0, 0))
PRINT "term size (piped fallback): ":SYSTEM(2):"x":SYSTEM(3)
PRINT "colors: ":LEN(COLOR("RED")):" ":LEN(COLOR("BRIGHT CYAN", "BLUE")):" ":LEN(COLOR("OFF")):" ":LEN(COLOR(196)):" ":LEN(@(-13)):" ":LEN(@(-18))
PRINT "bye"
END
