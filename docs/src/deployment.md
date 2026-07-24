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
companion to `CONVERT-ACCOUNT` (which rebuilds a whole account).

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

## Committing and cloning an account through git

Hash files are binary, so an account is not committed as-is. Two verbs
move an account between its **live** form (LMDB hash files) and a
**legible** form (one directory file per MV file, one text record per
Unix file) that git tracks with clean, mergeable diffs:

```
> EXPORT-ACCOUNT        live hash files  ->  legible directory files
$ scripts/mvx-convert.sh <account>       legible clone  ->  live hash files
```

`EXPORT-ACCOUNT` snapshots the whole account — the VOC, every
dictionary, and the record data of reference-sized files — into
directory files beside the store, then writes a `FILES` manifest
recording each file's backend. Bulk files (over `$MVXEXPORT_MAX`
records, default 5000) export their **dictionary only**, so a million
orders never land in git; their data is reloaded from its own source.
Commit the result with ordinary git (keep `mvxdata.lmdb` out via
`.gitignore`).

Every dictionary carries a `%FILE%` control record — written when the
file is created — that names the file's backend (and connection). It
*is* the file definition, so **committing the dictionary alone is
enough to recreate the file**: the data is effectively gitignored while
the schema, in git, rebuilds an empty file of the right type. `%FILE%`
is authoritative; the `FILES` manifest is only an optional override.

`CONVERT-ACCOUNT` — driven by `scripts/mvx-convert.sh` — is the
inverse, run once in a fresh clone. It moves the VOC and each
reference file's records into hash files (the driver named in `FILES`,
or a file marked `dir` stays a directory file), then hands off to
`BUILD` to create bulk data files empty from their dictionaries,
catalog `BP` source, and link packages. The clone becomes a working,
hash-file-backed account.

The two are inverses, so an account round-trips: `EXPORT-ACCOUNT` →
commit → clone → `mvx-convert.sh` → work → `EXPORT-ACCOUNT` → commit.
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
