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
 * @file FSDEMO
 * @version 2.0
 * Full-screen demo in Midnight Commander dress: cyan menu bar, blue
 * field, black-on-cyan marker, MC-style function-key bar. Arrows
 * move; HOME centres, END right edge, PGUP/PGDN page; q or ESC quits.
 * Resizing the terminal re-flows the chrome live.
 */
W = SYSTEM(2)
H = SYSTEM(3)
X = INT(W / 2)
Y = INT(H / 2)
K = ""
PRINT @(-5):@(-7):
LOOP
   W = SYSTEM(2)
   H = SYSTEM(3)
   IF X > W - 1 THEN X = W - 1
   IF Y > H - 3 THEN Y = H - 3
   * blue field under everything (BCE clear)
   PRINT COLOR("WHITE", "BLUE"):
   * menu bar, MC style
   HDR = "  Left     File     Command     Options     Right"
   HDR = HDR:STR(" ", 60):"pos ":X:",":Y:"  key ":K
   PRINT @(0, 0):COLOR("BLACK", "CYAN"):FMT(HDR, "L#":W):
   * the marker, drawn like an MC selection
   PRINT @(X, Y):COLOR("BLACK", "CYAN"):"X":COLOR("WHITE", "BLUE"):
   * function-key bar
   BAR = ""
   VIS = 0
   NUMS = " 1"
   LBL = "Help  "
   GOSUB 9000
   NUMS = " 4"
   LBL = "PgUp  "
   GOSUB 9000
   NUMS = " 5"
   LBL = "PgDn  "
   GOSUB 9000
   NUMS = " 6"
   LBL = "Home  "
   GOSUB 9000
   NUMS = " 7"
   LBL = "End  "
   GOSUB 9000
   NUMS = " q"
   LBL = "Quit"
   GOSUB 9000
   PAD = W - 1 - VIS
   IF PAD < 0 THEN PAD = 0
   PRINT @(0, H - 1):BAR:COLOR("BLACK", "CYAN"):STR(" ", PAD):COLOR("WHITE", "BLUE"):
   K = KEYIN()
UNTIL K = "q" OR K = "ESC" DO
   PRINT @(X, Y):COLOR("WHITE", "BLUE"):" ":
   BEGIN CASE
   CASE K = "UP"
      IF Y > 1 THEN Y = Y - 1
   CASE K = "DOWN"
      IF Y < H - 3 THEN Y = Y + 1
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
      IF Y < 1 THEN Y = 1
   CASE K = "PGDN"
      Y = Y + (H - 4)
      IF Y > H - 3 THEN Y = H - 3
   END CASE
REPEAT
PRINT COLOR("OFF"):@(-8):@(-6):
PRINT "bye"
STOP

* append one function-key segment to BAR (MC style: white-on-black
* number, black-on-cyan label)
9000
BAR = BAR:COLOR("WHITE", "BLACK"):NUMS:COLOR("BLACK", "CYAN"):LBL
VIS = VIS + LEN(NUMS) + LEN(LBL)
RETURN
