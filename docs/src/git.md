# Version Control

Hash-file records can be versioned with git **directly** — a record's
bytes become a git blob in memory, and a blob's bytes are written
straight back to the record. No copy to an intermediate file: the hash
file is git's source of truth. Attribute marks map to newlines in the
blob, so git diffs are legible and line-oriented, and back on restore.

## The git package

Linking `packages/git` adds a `GIT` verb. Git runs through **libgit2**
as native cataloged subroutines (no `git` binary, any privilege tier,
no shell — a commit message cannot inject), and git objects live in a
small bare repo (`.recgit`) in the account.

```
> GIT SAVE CUST added customers    commit CUST's records directly
> GIT LOG                          history
> GIT RESTORE CUST                 write the records back from git,
                                   deleting any absent from the commit
```

`GIT SAVE file {message}` reads each record, makes a blob, and commits
them under `file/<id>` — a change to one record is a one-line diff in
`file/<id>`. `GIT RESTORE file` mirrors the latest commit back into the
hash file: every recorded blob is written, and records absent from the
commit are deleted, so a revert or a checkout of an older commit
restores the file exactly.

Because the blobs are line-oriented text, ordinary git tooling works
on the record history:

```
git --git-dir=.recgit log --oneline
git --git-dir=.recgit diff HEAD~1 HEAD --stat
git --git-dir=.recgit show HEAD:CUST/C1
```

## EXPORT / IMPORT (materialised mirror)

When you want the records as real files on disk — to browse, to feed a
non-git tool, or to track BP source that is already a directory file —
`EXPORT` and `IMPORT` copy between a hash file and a **directory file**
(git-native: one record per Unix file, attributes as lines):

```
> EXPORT CUST                copies CUST's records into CUST.EXP
> IMPORT CUST                mirrors CUST.EXP back (full sync)
```

The difference: `GIT SAVE` versions records with no on-disk copy;
`EXPORT` produces a browsable directory file. Both round-trip records
faithfully.

## What to track

Per ARCHITECTURE.md 9:

| artifact | source of truth | approach |
|---|---|---|
| BP / source | the directory | git tracks the directory file natively |
| VOC / dictionaries | the hash file | `GIT SAVE` / `GIT RESTORE`, or `EXPORT`/`IMPORT` |
| data files | the backend store | usually not tracked |

Operational data is versioned by an explicit `GIT SAVE`, not synced
live, because a checkout must never rewrite verb dispatch underneath a
running session — the movement between store and history is a
deliberate command.
