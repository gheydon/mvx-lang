# Deployment and the Daemon

## Single host: embedded

The default. Each account directory carries its own
`mvxdata.lmdb` environment; concurrent sessions on one host share it
safely (LMDB single-writer, many readers). Directory files are plain
directories. Nothing to operate.

## Multi host: mvxd

LMDB must never live on a network filesystem. For more than one host,
`mvxd` owns the environment exclusively and serialises access:

```sh
mvxd -d /var/mvx/data -s /run/mvxd.sock     # or -p 4700 for TCP
```

Clients switch with one variable — the promised config swap:

```sh
export MVXDAEMON=/run/mvxd.sock             # or host:4700
mvx-tcl -a /path/to/account
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
> CREATE-FILE SHARED USING lmdbnet /run/mvxd.sock   networked LMDB
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
`mvxd`, and grant `unrestricted` only where a shell escape is truly
needed.
