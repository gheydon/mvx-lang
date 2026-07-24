# Statements

Reference, alphabetically. `{...}` marks optional parts.

### Assignment
```
var = expr
var<a{,v{,s}}> = expr        dynamic-array replace
ARR(i{,j}) = expr            dimensioned element
```

### BEGIN CASE
```
BEGIN CASE
CASE cond
   statements
CASE 1                       the catch-all
   statements
END CASE
```
The first true `CASE` wins.

### CALL
```
CALL NAME(args...)
CALL @VAR(args...)           indirect: VAR holds the subroutine name
```
Arguments pass by reference. Resolution happens at call time: the
program itself, then cataloged `LIB/` libraries, linked packages, and
the system account.

### COMMON
```
COMMON {/BLOCK/} item, item(dims), ...
```

### DIM
```
DIM A(rows{, cols})
```

### ECHO
```
ECHO ON | ECHO OFF
```
Terminal echo for `INPUT` (password entry).

### END / STOP / RETURN
`END` ends the program text. `STOP` ends the program immediately —
even from inside a subroutine. `RETURN` returns from a `GOSUB`, or
from a subroutine when the GOSUB stack is empty.

### EXECUTE
```
EXECUTE sentence {CAPTURING var} {RETURNING var}
```
Runs a TCL sentence in a child session. `CAPTURING` collects its
output as a dynamic array (line per attribute); `RETURNING` receives
the exit status. A select list formed by the sentence (e.g. an
EXECUTEd `SELECT`) is consumed by this program's next `READNEXT`.
`PERFORM` is a synonym.

### FOR / NEXT
```
FOR I = first TO last {STEP n}
   statements
NEXT I
```

### FORMLIST
```
FORMLIST dynarray
```
Installs the attributes of `dynarray` as the active select list.

### GOTO / GOSUB
```
GOTO label      GO TO label      GOSUB label ... RETURN
```
Labels are numbers at the start of a line. GOSUBs nest to depth 1024.

### IF
```
IF cond THEN statement {ELSE statement}

IF cond THEN
   statements
END ELSE
   statements
END
```
Single-line and block forms mix freely; `ELSE` without `THEN` is
allowed.

### INPUT
```
INPUT var
```
Reads one line from standard input. At end of input the program ends.

### LOOP
```
LOOP
   statements                pre-condition body
WHILE cond DO                or UNTIL cond DO
   statements                post-condition body
REPEAT
```

### LOCATE
```
LOCATE(item, dynarray{, attr#{, value#}}; setting {; order}) THEN/ELSE
```
Finds `item` at the given level. `setting` receives the position — on
ELSE, the insertion point (per `order`: `AL`, `AR`, `DL`, `DR`) or
count+1.

### MAT
```
MAT A = expr                 fill every element
MAT A = MAT B                copy (dimensions must match)
```

### PRINT / CRT
```
PRINT expr {: expr ...} {,} {:}
```
`:` between items concatenates; `,` advances to the next 18-column
zone; a trailing `:` suppresses the newline. Cursor addressing and
colours are just strings — see the terminal chapter.

### File statements
```
OPEN {"DICT",} "file" TO fvar THEN/ELSE
READ var FROM fvar, id THEN/ELSE
READU var FROM fvar, id THEN/ELSE     read with record lock
WRITE expr ON fvar, id                releases the lock
WRITEU expr ON fvar, id               keeps the lock
READV  var FROM fvar, id, n THEN/ELSE read attribute n of a record
READVU var FROM fvar, id, n THEN/ELSE same, with a record lock
WRITEV  expr ON fvar, id, n           replace attribute n, keep the rest
WRITEVU expr ON fvar, id, n           same, keeps the lock
MATREAD  arr FROM fvar, id THEN/ELSE  fields (@FM) -> array elements
MATREADU arr FROM fvar, id THEN/ELSE  same, with a record lock
MATWRITE  arr ON fvar, id             elements -> record; releases the lock
MATWRITEU arr ON fvar, id             keeps the lock
DELETE fvar, id
RELEASE {fvar, id}                    bare form releases all
SELECT fvar                           form the active select list
READNEXT var THEN/ELSE                next id from the active list
```
A select list left unconsumed when a program ends passes to the next
command in the TCL session.

## Conditional compilation

A source preprocessor runs before parsing, in the UniVerse/UniData
style, so the same source can target several MultiValue systems:

```
$DEFINE name {value}     define a symbol (optional replacement value)
$UNDEFINE name           remove a definition
$IFDEF name              keep the block if name is defined
$IFNDEF name             keep the block if name is NOT defined
$ELSE                    the other branch
$ENDIF                   end the conditional
```

`MVX` is predefined by the `mvx-basic` compiler; define your own on the
command line with `-D NAME` or `-D NAME=value`. A `$DEFINE` with a value
also substitutes that value wherever the name appears (outside string
literals). Inactive lines are blanked, not removed, so error messages
and the source-level debugger still line up with the original file.

```basic
$IFDEF MVX
   OPEN "ORDERS" TO F ELSE STOP
$ELSE
   OPEN "", "ORDERS" TO F ELSE STOP     ;* UniVerse two-argument form
$ENDIF
```

This is the portability seam: commit an account to git on one platform,
`mvx-git clone` it on MVX, and — with the `$IFDEF`s set correctly — it
compiles and runs.
