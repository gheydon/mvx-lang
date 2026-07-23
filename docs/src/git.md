# Version Control

`GIT` versions hash-file records and is modelled on real git — the
same verbs (`add`, `status`, `commit`, `log`, `diff`, `restore`, `rm`,
`show`), the same porcelain. The only difference is the unit: a "path"
is an MVX **file plus record id**. The **working tree** is the live
records; the **index** is a persistent staging area; commits are
commits, in a bare repo (`.recgit`) in the account.

Git runs through **libgit2** as native subroutines — no `git` binary,
any privilege tier, no shell (a commit message cannot inject).

```
> GIT INIT                       create the record repository
> GIT ADD CUST                   stage every record of CUST
> GIT ADD CUST C1                stage one record
> GIT STATUS
A  CUST/C1                       staged (added)
A  CUST/C2
> GIT COMMIT -m "initial customers"
[32f57ce] initial customers
> GIT LOG
32f57ce initial customers
```

After changing records in the hash file, status and diff behave like
git — the records are the working tree:

```
> GIT STATUS
 M CUST/C2                       modified, not staged
?? CUST/C3                       untracked (new record)
> GIT DIFF CUST
diff CUST/C2
 Bob
-Paris
+Berlin
> GIT RESTORE CUST               revert records to HEAD (drops C3)
> GIT SHOW CUST C1               committed content of a record
```

Status codes match git: `A` staged-added, `M` modified, `D` deleted,
`??` untracked, with the staged column first. `GIT RM file record`
unstages a record.

Because record blobs are line-oriented text (attribute marks become
newlines), ordinary git tooling reads the same history:

```
git --git-dir=.recgit log --oneline
git --git-dir=.recgit show HEAD:CUST/C1
```

## EXPORT / IMPORT (materialised mirror)

A separate tool: `EXPORT file` copies records into a **directory
file** (`file.EXP`) — real files on disk to browse or feed to
non-git tools — and `IMPORT file` mirrors them back. Use `GIT` to
version records in place; use `EXPORT` when you want the records as
files.

## What to track

| artifact | source of truth | approach |
|---|---|---|
| BP / source | the directory | git tracks the directory file natively |
| VOC / dictionaries | the hash file | `GIT ADD` / `COMMIT` / `RESTORE` |
| data files | the backend store | usually not tracked |
