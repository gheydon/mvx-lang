# The MVX BASIC Language

## Programs and subroutines

A **main program** is a sequence of statements; execution starts at
the top and ends at `END`, `STOP`, or falling off the end. A
**subroutine** begins with a `SUBROUTINE` declaration and returns with
`RETURN`:

```
SUBROUTINE GREET(NAME, RESULT)
RESULT = "hello ":NAME
RETURN
```

Arguments pass **by reference**: the callee can change the caller's
variables. Subroutines compile separately (into shared libraries when
cataloged) and are resolved at `CALL` time.

## Lines, labels and comments

One statement per line; `;` separates multiple statements on a line.
A number at the start of a line is a **statement label** for `GOTO`
and `GOSUB`.

Comments come in classic and C styles:

```
* classic full-line comment
! also classic
REM likewise
X = 1 ; * classic trailing comment
// C-style line comment
Y = 2 /* inline block */ + 3
/**
 * Block comments span lines - the usual home of docblocks.
 * @file MYPROG
 * @version 1.0
 */
```

C-style comments are an MVX extension. They cannot be confused with
division (`/` must be followed by an operand in valid code), and
newlines inside a block comment still end statements. The
`PORT-SOURCE` verb rewrites them as classic comments for legacy
platforms.

## Values

A variable holds a **dynamic string** that may also be a number.
Numbers never round-trip through text internally: arithmetic on
numeric values is native, and the compiler specialises provably
numeric variables and arrays down to machine integers and doubles.
None of that changes semantics — it is why MVX is fast.

String comparison follows MV rules: if both operands look numeric,
they compare numerically; otherwise byte-wise. An unassigned variable
warns once and reads as zero / empty.

Literals: `123`, `12.5`, `"text"`, `'text'`.

## Operators

| | |
|---|---|
| arithmetic | `+  -  *  /  ^` (power) |
| string | `:` concatenation |
| comparison | `=  #  <  >  <=  >=` (also `EQ NE LT GT LE GE`) |
| logic | `AND  OR  NOT(x)` |
| substring | `X[start, len]` (1-based) |
| format | `X "R#10"` — apply a format mask (also `FMT(X, mask)`) |

## Dynamic arrays

The heart of MV data. A dynamic array is a string with **attribute**
(`@AM`), **value** (`@VM`) and **subvalue** (`@SM`) marks:

```
REC = "Widget":@AM:"9.99":@AM:"blue"
PRINT REC<2>              -> 9.99
REC<2> = "10.50"          replace attribute 2
REC<-1> = "new"           append an attribute
V = REC<1,2>              value 2 within attribute 1
```

Angle-bracket extraction and replacement work at all three levels and
pad with marks when writing past the end. The function forms
`EXTRACT`, `REPLACE`, `INSERT`, and `DELETE` do the same work on
copies. `LOCATE` searches a level, optionally maintaining sort order —
the classic idiom for both membership tests and ordered insertion.

## Dimensioned arrays

`DIM A(10)` or `DIM T(5, 4)`: fixed-size, 1-based, bounds-checked.
`MAT A = 0` fills; `MAT A = MAT B` copies. Dimensioned arrays are
distinct from dynamic arrays and compile to flat native storage when
their contents are provably numeric.

## COMMON

`COMMON A, B, ARR(10)` declares variables shared positionally between
a main program and everything it CALLs; `COMMON /NAME/ ...` names a
block. COMMON storage lives in the session, so separately compiled
subroutines see the same data.
