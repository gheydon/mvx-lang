# Full-Screen Terminal Programming

The primitives behind `FSDEMO` and `SNAKE` (see `examples/`).

## Decoded keystrokes: KEYIN

`KEYIN()` blocks for one keystroke and returns it **decoded** — never
raw escape bytes:

- printable characters as themselves (UTF-8 delivered whole)
- specials by name: `UP DOWN LEFT RIGHT HOME END PGUP PGDN DEL INS
  F1..F12 ENTER TAB BS ESC BTAB`
- control keys as `^A` .. `^Z`

`KEYIN(ms)` waits at most `ms` milliseconds and returns `""` on
timeout — that timeout is a game tick, a status refresh, a "press any
key within 5 seconds". Raw terminal mode is entered and restored per
call; a crashed program cannot wedge the terminal. Both CSI and SS3
(application cursor mode) encodings decode, so the same program works
in every terminal.

## Cursor addressing and screen codes: @()

`PRINT @(col, row):"text":` positions and writes (0-based, classic).
Negative codes:

| | |
|---|---|
| `@(-1)` | clear screen and home |
| `@(-2)` | home |
| `@(-3)` / `@(-4)` | clear to end of screen / of line |
| `@(-5)` / `@(-6)` | enter / leave the **alternate screen** |
| `@(-7)` / `@(-8)` | hide / show the cursor |
| `@(-11)`/`@(-12)` | blink on / off |
| `@(-13)`/`@(-14)` | reverse on / off |
| `@(-15)`/`@(-16)` | underline on / off |
| `@(-17)` | bold |
| `@(-18)` | all attributes off |
| `@(col)` | column move on the current row |

**Always bracket full-screen work with `@(-5)` ... `@(-6)`.** The
geometry from `SYSTEM(2)`/`SYSTEM(3)` is only guaranteed true on the
alternate screen (terminals like Warp decorate the main screen), and
leaving it restores the user's scrollback.

## Colours: COLOR

```
PRINT COLOR("BRIGHT YELLOW"):"warning":COLOR("OFF")
PRINT COLOR("WHITE", "RED"):" ALERT ":COLOR("OFF")
PRINT COLOR(196):"palette colour 196":COLOR("OFF")
```

Names: `BLACK RED GREEN YELLOW BLUE MAGENTA CYAN WHITE`, optional
`BRIGHT` prefix; numbers 0–255 use the extended palette; `OFF` (or
`RESET`) clears colours and attributes.

Colour codes are invisible characters — `LEN()` cannot measure a
coloured string for layout. Track visible width separately (see the
function-key bar in `examples/FSDEMO.b`).

## Size, echo, timing

- `SYSTEM(2)` / `SYSTEM(3)` — live terminal width and depth (re-read
  them to notice a resize).
- `ECHO OFF` / `ECHO ON` — input echo, for password prompts.
- `SYSTEM(12)` — millisecond clock for frame timing.

## The shape of a full-screen program

```
PRINT @(-5):@(-7):                       enter alt screen, hide cursor
LOOP
   W = SYSTEM(2) ; H = SYSTEM(3)         notice resizes
   ... draw ...
   K = KEYIN(tick)
UNTIL K = "q" OR K = "ESC" DO
   ... react ...
REPEAT
PRINT COLOR("OFF"):@(-8):@(-6):          restore everything
```
