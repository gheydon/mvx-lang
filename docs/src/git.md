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

## What to track: GITIGNORE and dictionaries

You do not commit a million order records — but you do commit their
schema. `GITIGNORE` (a record in the account, one glob per line) keeps
matching files and records out of history; `GIT IGNORE pattern`
appends to it. Dictionaries are addressed as `DICT`:

```
> GIT IGNORE ORDERS               keep the bulk order data out
> GIT ADD ORDERS                  ORDERS is in GITIGNORE, nothing staged
> GIT ADD DICT ORDERS             stage the ORDERS dictionary (schema)
> GIT STATUS
A  ORDERS.DICT/CUST               only the schema is tracked
```

A pattern matches a file name (`ORDERS`), a glob (`*.TMP`,
`LOG*`), or a `file/record` path (`ORDERS/O*`). Ignored records never
appear as untracked, so `GIT STATUS` stays quiet about bulk data. The
usual shape is to ignore data files and `GIT ADD DICT` their
dictionaries, plus `GIT ADD VOC` for verbs — schema and configuration
in git, data in the store.

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
| data files | the backend store | ignored via `GITIGNORE` |

## Delivery: stock, sites, and upstream features

The perennial MultiValue problem — ship a stock product to many sites,
let each customise, and fold good customisations back into the
mainline — is git branching. The record repo makes it concrete:

```
                stock mainline = the main branch
> GIT BRANCH site-acme          deploy stock to a site (a branch)
> GIT CHECKOUT site-acme        switch: the records update to match
  ... customise: edit verbs, dictionaries, add records ...
> GIT ADD ... ; GIT COMMIT -m "acme: custom dashboard"

  roll a site feature upstream into stock:
> GIT CHECKOUT main
> GIT CHERRY-PICK site-acme     apply just that commit to the mainline

  push new stock down to a site:
> GIT CHECKOUT site-acme
> GIT MERGE main                3-way merge; records update
```

`GIT CHECKOUT` switches the branch **and materialises its records into
the live hash files** — the working tree is the data, so a checkout
updates the records (writing changes, deleting records absent from the
branch). `GIT MERGE` and `GIT CHERRY-PICK` do real 3-way merges of
records, dictionaries, and verbs: when a site and the mainline touched
different records they merge cleanly; when they touched the same
record git reports the conflict for you to resolve (edit the record,
`GIT ADD`, `GIT COMMIT`). `GIT BRANCH` lists or creates branches.

Because it is real git underneath, the mainline can live in a shared
repository and sites can `clone`/`pull`/`push` it once network
transport is added — the branching model above is what makes stock
delivery and feature round-tripping tractable across many client
sites.

## Cloning and rebuilding an account

An account committed to git carries its *configuration* — dictionaries
(as `<file>.DICT` directory files), BP source, VOC, `PACKAGES`,
`FILES`, `GITIGNORE` — but not its data (kept out of git by
`GITIGNORE`). After a clone you have the schema and source but no data
files. `BUILD` provisions a working account from what is there:

```sh
git clone <account-repo> mysite && cd mysite
mvx -a . -c BUILD          # developer privilege (it catalogs)
```

`BUILD`:

1. ensures the account (`VOC`) exists;
2. for each dictionary present without its data file, **creates the
   file** — the type comes from the `FILES` manifest override, then the
   `%FILE%` control record stored in the dictionary itself, then an
   interactive prompt (`MVXBUILD_ASK=1`), then the `lmdb` default — and
   imports the dictionary, rebuilding indexes from `%INDEXES%`;
3. catalogs BP source into runnable verbs;
4. links the packages listed in `PACKAGES`.

This is why a `.DICT` directory with no data file is enough to
rebuild: it tells `BUILD` a file exists, what its schema is, and — via
the `%FILE%` control record `CREATE-FILE` stamped in — what backend to
make it on and which indexes to rebuild. A `FILES` manifest is only
needed to override those hints (e.g. a client putting a file on a
different backend than the vendor used). Prepare an account for
delivery by exporting each dictionary — `EXPORT DICT PARTS` writes
`PARTS.DICT` — and declaring types in `FILES`; the data stays in the
store. Packages are themselves account-shaped, so the same `BUILD`
provisions a cloned package.
## mvx-git — the drop-in git wrapper

`GIT` (above) version-controls records *inside* a running account. For
the outside — cloning, pulling, and switching branches of an account's
git repository from the command line — the git package ships
**`mvx-git`**, a small native command that wraps the real `git`.

Every command is forwarded verbatim, so `mvx-git` is a complete git
replacement — you can `alias git=mvx-git`. The one addition: after a
command that changes the working tree (`clone`, `checkout`, `switch`,
`pull`, `merge`, `rebase`, `reset`, `restore`, `cherry-pick`, `revert`,
`stash`) succeeds in a directory that is an MVX account (it carries a
`.mvx` descriptor), `mvx-git` rebuilds the account — exactly
`mvx -a <account> -c CONVERT-ACCOUNT` — so the hash files match the
git-tracked legible form. In an ordinary repository it does nothing
extra and behaves precisely like git.

```sh
mvx-git clone git@host:acct.git myacct   # git clone, then build hash files
cd myacct
mvx-git pull                              # git pull, then re-sync the account
mvx-git checkout release-2            # switch branch, rebuild records
```

**Clone provisions a new account.** A clone lands in a fresh directory,
so mvx-git treats it specially:

- if the cloned repo carries a `.mvx` descriptor it *is* an account —
  mvx-git rebuilds it automatically (`CONVERT-ACCOUNT`);
- if it does not, and stdin is a terminal, mvx-git asks
  *"Directory … is not an MVX account. Create one here? (y/N)"* —
  defaulting to no. On yes it creates one with `BUILD`, which only adds
  a `.mvx` and a `VOC` (and catalogs any `BP`), so declining leaves an
  ordinary checkout and an accidental yes cannot mangle a non-MVX repo.

Set `MVXGIT_CREATE=1` to answer yes without prompting (for scripts and
the coming package installer); with no terminal and no override,
mvx-git never prompts and never creates an account.

`mvx-git` changes no MVX internals — it only orchestrates the real
`git` and the existing `CONVERT-ACCOUNT` verb. It is built by the git
package's `build-native.sh` into `packages/git/bin/`, which `mkpkg.sh`
links onto the dev `PATH` (`build/bin`); the real git is found on
`PATH`, the MVX shell as `$MVX` or `mvx` on `PATH`.
