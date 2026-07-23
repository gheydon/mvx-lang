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
DELETE fvar, id
RELEASE {fvar, id}                    bare form releases all
SELECT fvar                           form the active select list
READNEXT var THEN/ELSE                next id from the active list
```
A select list left unconsumed when a program ends passes to the next
command in the TCL session.
