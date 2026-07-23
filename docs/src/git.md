# Version Control

Hash-file records live in a keyed store, but git works on text files.
MVX bridges the two by **exporting a hash file into a directory file**
— and directory files are already git-native (one record per Unix
file, attributes as lines), so git tracks them with no sync logic.

## EXPORT / IMPORT

```
> EXPORT CUST                copy CUST's records into CUST.EXP (a
                             directory file), git-native
> IMPORT CUST                mirror CUST.EXP back into CUST
```

`EXPORT file {dir}` writes each record (respecting an active select
list) as a text file in the directory file `dir` (default
`<file>.EXP`). `IMPORT file {dir}` is a **full mirror**: every record
in the directory is written to the file, and file records absent from
the directory are deleted — so a checkout, merge, or pull that added,
changed, or removed record files is reflected exactly.

## The git package

Linking `packages/git` adds a `GIT` verb whose subcommands wrap the
workflow. Git operations run through **libgit2** as native cataloged
subroutines (in the package's `LIB/`), not by spawning the `git` CLI —
so they need no external binary, work at any privilege tier, and take
structured arguments (a commit message cannot inject into a shell):

```
> GIT INIT                   git init in the account
> GIT EXPORT CUST            EXPORT, then git add CUST.EXP
> GIT COMMIT saved customers git commit -m ...
> GIT STATUS / LOG / DIFF    the usual
> GIT IMPORT CUST            after a pull/checkout, mirror back
```

Local plumbing (init, add, commit, status, log, diff) is native.
Network operations (clone, push, pull) still need credential handling
and are a later addition.

A change to a record produces a one-line git diff in
`CUST.EXP/<id>`, so history is legible and merges behave. The typical
cycle: edit records (with `ED`, `VI`, or a program), `GIT EXPORT`,
`GIT COMMIT`; and on another machine, `git pull` then `GIT IMPORT`.

## What to track

Per ARCHITECTURE.md 9:

| artifact | source of truth | git |
|---|---|---|
| BP / source | the directory | native — edit in place, no export |
| VOC / dictionaries | the hash file | `EXPORT`/`IMPORT` a text mirror |
| data files | the backend store | usually not tracked |

Source in a `BP` directory file is tracked directly. Operational data
— VOC, dictionaries — is exported explicitly rather than synced live,
because a `git checkout` must not silently rewrite verb dispatch
underneath a running session. Movement between store and repository is
a deliberate `EXPORT`/`IMPORT`, like `pg_dump`/`psql`.
