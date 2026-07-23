# Intrinsic Functions

### Strings

| | |
|---|---|
| `LEN(s)` | length |
| `TRIM(s)` | strip leading/trailing blanks, squeeze runs |
| `STR(s, n)` | s repeated n times |
| `SPACE(n)` | n spaces |
| `CHAR(n)` / `SEQ(s)` | code to character and back |
| `FIELD(s, delim, n {, cnt})` | n-th delimited field (cnt fields with delimiters) |
| `INDEX(s, sub, occ)` | position of the occ-th occurrence, 0 if none |
| `COUNT(s, sub)` | occurrences of sub |
| `DCOUNT(s, delim)` | delimited-field count |
| `NUM(x)` | 1 if x is numeric ("" counts) |
| `s[start, len]` | substring (operator) |
| `CHANGE(s, old, new)` | replace every occurrence of old with new |
| `FMT(x, mask)` | format: `L`/`R` justify, fill `#` space `*` star `%` zero, width — `FMT(N, "R%8")` |

### Dynamic arrays

| | |
|---|---|
| `EXTRACT(A, a{,v{,s}})` | same as `A<a,v,s>` |
| `REPLACE(A, a{,v{,s}}, x)` | copy with element replaced |
| `INSERT(A, a{,v{,s}}, x)` | copy with element inserted |
| `DELETE(A, a{,v{,s}})` | copy with element removed |

System constants: `@AM`/`@FM` (attribute mark), `@VM` (value mark),
`@SM`/`@SVM` (subvalue mark).

### Numbers

| | |
|---|---|
| `INT(x)` | truncate toward zero |
| `ABS(x)`, `SQRT(x)` | as expected |
| `MOD(a, b)` | modulo, sign of b |
| `RND(n)` | random 0 .. n-1 |

### Date and time

| | |
|---|---|
| `DATE()` | internal date (day 0 = 31 DEC 1967), local |
| `TIME()` | seconds since midnight, local |
| `SYSTEM(12)` | milliseconds since midnight |
| `OCONV(x, code)` | internal → external |
| `ICONV(x, code)` | external → internal |

Conversion codes: `D{y}{sep}` dates (`OCONV(0,"D")` → `31 DEC 1967`,
`D2/` → `12/31/67`), `MT{S}` times (`MTS` → `12:34:56`), `MD{n}[,][$]`
masked decimal (`OCONV(1234567,"MD2,$")` → `$12,345.67`), `MCU`,
`MCL`, `MCT` case. `STATUS()` after a conversion: 0 ok, 1 bad input,
2 bad code.

### Environment and session

| | |
|---|---|
| `SENTENCE()` | the TCL command line that invoked this program |
| `ENV(name)` | environment variable, "" if unset |
| `STATUS()` | last conversion status |
| `SYSTEM(2)` / `SYSTEM(3)` | terminal width / depth (live) |
| `SYSTEM(11)` | 1 when a select list is active |

### Terminal

| | |
|---|---|
| `KEYIN({ms})` | one decoded keystroke; with ms, "" on timeout |
| `@(col, row)` | cursor positioning string |
| `@(-1..-18)` | screen codes — see the terminal chapter |
| `COLOR(fg {, bg})` | colour codes — see the terminal chapter |

### OS files

For moving records to and from the outside world (editors, git, diff).

| | |
|---|---|
| `OSREAD(path)` | whole OS file as a string; `STATUS()` 1 on failure |
| `OSWRITE(data, path)` | write; returns 1/0 |
| `OSDELETE(path)` | remove; returns 1/0 |
| `TMPNAM()` | a fresh temp path |
| `EDITFILE(path)` | run the external editor (unrestricted tier) |

Export a record as attribute-per-line text and back:

```
LF = CHAR(10)
X = OSWRITE(CHANGE(REC, @AM, LF), PATH)     record -> text file
REC = CHANGE(OSREAD(PATH), LF, @AM)          text file -> record
```

### Files, indexes, system

| | |
|---|---|
| `CREATEFILE(name {, "DIR"})` | create a file (data + dictionary) |
| `DELETEFILE(name)` | remove a file |
| `FILELIST()` | the account's files, name @VM type per attribute |
| `INDEXBUILD(fvar, item)` | (re)build an index; returns record count |
| `INDEXDROP(fvar, item)` | drop an index |
| `INDEXSELECT(fvar, item, value)` | form the select list from an index |
| `COMPILE(mode, src, out)` | spawn the compiler (developer tier); mode `"c"`, `"exe"`, `"shared"` |
| `DOCTAG(tag)` | *inside an I-type dictionary item*: extract "@tag value" from a record's comments |
