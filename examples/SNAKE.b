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
 * @file SNAKE
 * @version 1.0
 * Snake, in MVX BASIC. Arrows steer; eating food grows the snake and
 * speeds the game; walls and your own tail end it. q or ESC quits.
 * The game tick is KEYIN with a timeout; the body is a dynamic array
 * navigated with LOCATE / INSERT / DELETE.
 */
W = SYSTEM(2)
H = SYSTEM(3)
TOP = 1
BOT = H - 2
SCORE = 0
DX = 1
DY = 0
HX = INT(W / 2)
HY = INT(H / 2)
BODY = ""
FOR I = 0 TO 3
   BODY<-1> = (HX - I):",":HY
NEXT I
PRINT @(-5):@(-7):
* draw the initial snake
N = DCOUNT(BODY, @AM)
FOR I = 2 TO N
   P = BODY<I>
   PRINT @(FIELD(P, ",", 1), FIELD(P, ",", 2)):COLOR("GREEN"):"o":
NEXT I
PRINT @(HX, HY):COLOR("BRIGHT GREEN"):"@":
GOSUB 9000
GOSUB 9100
LOOP
WHILE 1 DO
   TICK = 130 - SCORE * 3
   IF TICK < 45 THEN TICK = 45
   K = KEYIN(TICK)
   BEGIN CASE
   CASE K = "q" OR K = "ESC"
      GOTO 500
   CASE K = "UP"
      IF DY # 1 THEN DX = 0 ; DY = -1
   CASE K = "DOWN"
      IF DY # -1 THEN DX = 0 ; DY = 1
   CASE K = "LEFT"
      IF DX # 1 THEN DX = -1 ; DY = 0
   CASE K = "RIGHT"
      IF DX # -1 THEN DX = 1 ; DY = 0
   END CASE
   NX = HX + DX
   NY = HY + DY
   IF NX < 0 OR NX > W - 1 OR NY < TOP OR NY > BOT THEN GOTO 400
   NP = NX:",":NY
   LOCATE(NP, BODY; Q) THEN GOTO 400
   * old head becomes body
   PRINT @(HX, HY):COLOR("GREEN"):"o":
   BODY = INSERT(BODY, 1, 0, 0, NP)
   HX = NX
   HY = NY
   PRINT @(HX, HY):COLOR("BRIGHT GREEN"):"@":
   IF NP = FOOD THEN
      SCORE = SCORE + 1
      GOSUB 9000
      GOSUB 9100
   END ELSE
      N = DCOUNT(BODY, @AM)
      TP = BODY<N>
      PRINT @(FIELD(TP, ",", 1), FIELD(TP, ",", 2)):" ":
      BODY = DELETE(BODY, N, 0, 0)
   END
REPEAT

* game over
400
MSG = "  GAME  OVER  -  score ":SCORE:"  -  any key  "
PRINT @(INT((W - LEN(MSG)) / 2), INT(H / 2)):COLOR("WHITE", "RED"):MSG:COLOR("OFF"):
K = KEYIN()
* leave
500
PRINT COLOR("OFF"):@(-8):@(-6):
PRINT "final score: ":SCORE
STOP

* ---- 9000: place food off the snake ------------------------------------
9000
FX = RND(W)
FY = RND(BOT - TOP + 1) + TOP
FP = FX:",":FY
LOCATE(FP, BODY; Q9) THEN GOTO 9000
FOOD = FP
PRINT @(FX, FY):COLOR("BRIGHT RED"):"*":
RETURN

* ---- 9100: score bar ---------------------------------------------------
9100
PRINT @(0, 0):COLOR("BLACK", "GREEN"):FMT("  SNAKE   score ":SCORE:"   arrows steer, q quits", "L#":W):COLOR("OFF"):
RETURN
