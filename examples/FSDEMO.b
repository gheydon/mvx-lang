/**
 * @file FSDEMO
 * @version 1.4
 * A tiny full-screen application, sized to the terminal: arrows move
 * the X; HOME centres, END jumps to the right edge, PGUP/PGDN
 * page vertically; q or ESC quits. Bounds re-read every keystroke,
 * so resizing the window adjusts the playfield live.
 */
W = SYSTEM(2)
H = SYSTEM(3)
X = INT(W / 2)
Y = INT(H / 2)
* alternate screen: true full-size grid in every terminal, and the
* scrollback comes back on exit; hide the cursor while we draw
PRINT @(-5):@(-7):
LOOP
   W = SYSTEM(2)
   H = SYSTEM(3)
   IF X > W - 1 THEN X = W - 1
   IF Y > H - 2 THEN Y = H - 2
   PRINT @(0, 0):@(-4):COLOR("BRIGHT WHITE", "BLUE"):" MVX full-screen demo  ":W:"x":H:"  arrows/HOME/END/PGUP/PGDN move, q quits ":COLOR("OFF")
   PRINT @(0, 1):STR("-", W - 1):
   PRINT @(X, Y):COLOR("BRIGHT YELLOW"):"X":COLOR("OFF"):@(0, H - 1):@(-4):"pos ":X:",":Y:"  key ":K:
   K = KEYIN()
UNTIL K = "q" OR K = "ESC" DO
   PRINT @(X, Y):" ":
   BEGIN CASE
   CASE K = "UP"
      IF Y > 2 THEN Y = Y - 1
   CASE K = "DOWN"
      IF Y < H - 2 THEN Y = Y + 1
   CASE K = "LEFT"
      IF X > 0 THEN X = X - 1
   CASE K = "RIGHT"
      IF X < W - 1 THEN X = X + 1
   CASE K = "HOME"
      X = INT(W / 2)
      Y = INT(H / 2)
   CASE K = "END"
      X = W - 1
   CASE K = "PGUP"
      Y = Y - (H - 4)
      IF Y < 2 THEN Y = 2
   CASE K = "PGDN"
      Y = Y + (H - 4)
      IF Y > H - 2 THEN Y = H - 2
   END CASE
REPEAT
PRINT @(-8):@(-6):
PRINT "bye"
