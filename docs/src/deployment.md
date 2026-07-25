# Deployment and the Daemon

## Single host: embedded

The default. Each account directory carries its own
`mvxdata.lmdb` environment; concurrent sessions on one host share it
safely (LMDB single-writer, many readers). Directory files are plain
directories. Nothing to operate.

## Multi host: mvx-lmdbd

LMDB must never live on a network filesystem. For more than one host,
`mvx-lmdbd` owns the environment exclusively and serialises access:

```sh
mvx-lmdbd -d /var/mvx/data -s /run/mvx-lmdbd.sock     # or -p 4700 for TCP
```

Clients switch with one variable — the promised config swap:

```sh
export MVXDAEMON=/run/mvx-lmdbd.sock             # or host:4700
mvx -a /path/to/account
```

With only `$MVXDAEMON` set, every LMDB-backed file — data,
dictionaries, indexes, the VOC — lives in the daemon; directory files
(BP source) stay local. No application change, no relink: the
`lmdbnet` driver presents the identical contract over the wire.

## Choosing a backend per file

A file's backend is named at creation:

```
> CREATE-FILE ORDERS                          local LMDB (default)
> CREATE-FILE ARCHIVE DIR                      directory (git-native source)
> CREATE-FILE SHARED USING lmdbnet /run/mvx-lmdbd.sock   networked LMDB
> CREATE-FILE HOT USING lmdbnet               networked, default $MVXDAEMON
```

`USING <driver> {connection}` binds the file to a storage driver —
`lmdbnet` today, and `postgres`/`mongo` as they arrive — recording it
in the account's `BINDINGS` record and creating the file through that
driver. `DELETE-FILE` removes the file and its binding; `LISTF` shows
each file's backend by name (`lmdb`, `directory`, `lmdbnet`, ...).
Different files may use different drivers and connections, so one
account can mix local LMDB, networked files on several daemons,
directory files, and (later) SQL or document stores — and one program
reads them all through ordinary `OPEN`/`READ`/`WRITE`.

The binding is a plain, hand-editable record: `SPEC driver {params}`
per line (`*` as the spec binds every LMDB file; `params` is the
driver's connection string, defaulting to `$MVXDAEMON` for lmdbnet).
With no `BINDINGS` record, bare `$MVXDAEMON` binds the whole account
to `lmdbnet` — the simple all-networked deployment. Binding is
resolution only; existing data does not move when it changes.

To actually move a file to a different backend, use **`CONVERT-FILE`**:

```
> CONVERT-FILE ORDERS dir                         hash file -> directory file
> CONVERT-FILE ORDERS lmdb                        directory file -> hash file
> CONVERT-FILE ORDERS USING lmdbnet /run/mvx.sock   local -> networked
```

`CONVERT-FILE file newtype {connection}` re-keys the file's records —
and its dictionary — into the new backend verbatim, so a hash file
round-trips to a directory file and back without loss, and the `%FILE%`
control record is restamped to the new type. It is the per-file
companion to mvx-convert-acct (which rebuilds a whole account).

What the daemon guarantees:

- **Single lock authority.** `READU` locks are granted by the daemon
  and **leased to the connection**: a client that dies without
  `RELEASE` loses its locks the moment the connection drops. No
  orphaned locks from killed pods.
- **Index atomicity.** A record write and its index updates commit in
  one daemon-side transaction.
- **Snapshot selects.** `SELECT` materialises the id list before
  sending; a slow client never pins a read transaction.

Costs, accepted deliberately: the daemon is a single point of
failure, the single-writer throughput ceiling remains, and
replication/HA is not provided. Protocol integers are host-order —
keep daemon and clients on the same architecture for now.

## Multiple accounts on one daemon

Several accounts can share one `mvx-lmdbd`. Understand the model before
you point production accounts at a shared daemon:

- **One flat, shared file namespace — no account isolation.** The daemon
  addresses a file by its **bare name** (the account is not part of the
  key). Two accounts that each bind a file called `ORDERS` to the same
  daemon read and write the **same** data. That is the point of the
  shared-data pattern (`docker/compose.demo.yaml`), but there is
  currently **no way to keep unrelated same-named files private** per
  account on one daemon — put private files on a local backend, or run a
  separate daemon per trust domain.
- **Locking is coordinated across accounts.** The daemon — not the
  client — is the single lock authority, so a `READU` on `ORDERS`/`O1`
  from one account genuinely blocks another account's `READU` on the
  same record. Locks are keyed by file name + record id and leased to
  the connection (dropped on disconnect).
- **No authentication or authorization.** Any client that can reach the
  port can read, write, create, lock, and **`DELETE-FILE` (drop)** any
  file — there is no login and no per-file permission. Treat the daemon
  as trusted infrastructure: bind it to a private network (as the demo
  does), never expose the port publicly.
- **No drop protection.** `DELETE-FILE` drops the shared database
  immediately, with no reference counting — one account can remove a
  file another account is actively using.
- **Dictionaries are per-account views.** A remote data file's
  dictionary (and its `%FILE%`/`%INDEXES%` control records) lives with
  the account that binds it, so different accounts can hold different
  column definitions over the same shared data. Index *data* is shared
  on the daemon; index *definitions* are local, so building an index in
  one account does not advertise it to another.
- **Whole-account `$MVXDAEMON` binding sees every file on the daemon.**
  `LISTF` through a whole-account daemon binding enumerates all files the
  daemon holds, across accounts. Per-file `BINDINGS` is the way to attach
  only the specific shared files an account should see.
- **Single-threaded, serialised.** Requests are handled one at a time,
  so a slow or partial client can stall others (head-of-line blocking).

If you need isolation, authentication, or drop protection, follow the
issues linked from the multi-account investigation
([#5](https://github.com/mvx-lang/mvx/issues/5)).

## Committing and cloning an account through git

Hash files are binary, so an account is never committed as-is. Git
tracks the **directory form** — each file as `NAME/` (one text file per
record) beside `NAME.DICT/` (its dictionary) — while a **live account is
hash files with no `.DICT`**. `mvx-convert-acct` moves between the two:

```
mvx-convert-acct <account>            directory form -> live hash files
mvx-convert-acct --export <account>   live hash files -> directory form
```

On **import**, every file is rebuilt as the backend its dictionary's
`%FILE%` control record names — a hash file for an `lmdb` file, a
directory file (keeping its `NAME.DICT`) for a `dir` file — then `BUILD`
catalogs `BP` and links packages. On **export**, each hash file is
written out as `NAME/` + `NAME.DICT/`, copying the dictionary verbatim
so its `%FILE%` still records the real backend. `%FILE%` is the file
definition, so a committed dictionary alone recreates its file.

You rarely call `mvx-convert-acct` directly — **`mvx-git` does it for
you**: it rebuilds the account after `clone`/`checkout`/`pull` and
exports it before `commit`/`add` (see [Version Control](git.md)). Run
`mvx-convert-acct` by hand only for a *plain* `git` checkout — the
bootstrap that turns mvx-lang's own `system` account and packages into
real accounts before `mvx-git` exists.

This is the stock-to-site delivery path — combined with record-level
branch, merge, and cherry-pick (see [Version Control](git.md)), a
customised site tracks stock upstream and feeds features back.

## The migration curve

```
embedded LMDB  ->  networked LMDB  ->  (heavier backends)
```

Each step is configuration, not surgery, because the driver contract
is the boundary. Migration can be per file: a directory file, an
embedded file and a daemon file coexist in one account.

## Privilege and containers

`$MVXPRIV` comes from the process environment — system-level
configuration that account data cannot write. In containers, run app
sessions stateless with the account on a volume or entirely behind
`mvx-lmdbd`, and grant `unrestricted` only where a shell escape is truly
needed.
