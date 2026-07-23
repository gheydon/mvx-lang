# TCL and Verbs

`mvx-tcl` is the classic shell: a prompt, a builtin table, VOC
dispatch, and nothing else. Verbs are compiled BASIC programs.

```sh
mvx-tcl -a /path/to/account          # log on to an account
mvx-tcl -a acct -c "LIST PARTS"      # one command (ssh/cron style)
```

Interactive sessions have arrow-key history (persisted in
`~/.mvx_history`) and the prompt names the account.

**Resolution order** for a typed command: builtins → the account VOC →
linked packages (in order) → the system account. Local entries
override everything; the system account is the source of truth for
standard verbs.

## Builtins

| | |
|---|---|
| `OFF` / `QUIT` / `BYE` | end the session |
| `LOGTO dir` | switch accounts |
| `! command` | raw Unix (unrestricted tier) |
| `SH` | interactive shell (unrestricted tier) |

## Standard verbs

| verb | |
|---|---|
| `CREATE-FILE name {DIR\|REMOTE {addr}}` | create a file — local, directory, or daemon-backed |
| `DELETE-FILE name` | delete a file |
| `CLEAR-FILE name` | delete every record |
| `LISTF` | the account's files |
| `COUNT {DICT} file` | record count (uses an active select list) |
| `LIST {DICT} file {items} {WITH item op value} {BY item}` | query |
| `SELECT {DICT} file {WITH item op value}` | form a select list for the next command |
| `CT {DICT} file id` | show a record, numbered attributes |
| `COPY file id TO {file2} id2` | copy a record |
| `DELETE file id {id...}` | delete records |
| `ED file id` | the line editor (see below) |
| `BASIC file item` | compile to an object (developer) |
| `CATALOG file item` | compile, link and publish (developer) |
| `PORT-SOURCE file item {TO file2} {item2}` | rewrite C-style comments as classic |
| `CREATE-INDEX file item` / `DELETE-INDEX` / `LIST-INDEXES` | secondary indexes |
| `LINK-PKG path` / `UNLINK-PKG path` / `LIST-PKGS` | packages |
| `WHO` `TIME` `DATE` | session information |
| `GIT ...` | from the git package: status, log, diff, init |

## Select lists between commands

`SELECT` leaves its list for the next command:

```
> SELECT PARTS WITH COLOR = blue
2 record(s) selected
> LIST PARTS NAME PRICE          only the selected records
```

The list is consumed exactly once. Inside a program, `EXECUTE "SELECT
..."` followed by `READNEXT` uses the same mechanism.

## ED

The classic line editor: `ED FILE ID`.

| | |
|---|---|
| Enter | next line |
| *n* | go to line n |
| `L {n}` | list n lines |
| `T` / `B` | top / bottom |
| `I` | insert after current line — `.` alone ends input |
| `DE {n}` | delete n lines |
| `R/old/new` | replace in the current line |
| `FI` | file (save) and exit |
| `EX` | exit without saving |

## The privilege gate

Every spawn — `!`, `SH`, `EXECUTE`, and the compiler — passes through
one gate in the runtime, controlled by `$MVXPRIV`:

| tier | may |
|---|---|
| restricted (default) | run verbs, EXECUTE sentences |
| developer | + compile (`BASIC`, `CATALOG`, `COMPILE()`) |
| unrestricted | + raw Unix (`!`, `SH`) |

The check lives in the runtime because anyone who can compile could
call the primitives directly — a shell-only check would be
decorative.
