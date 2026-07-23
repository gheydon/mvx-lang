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

## Mixing local and remote per file

Migration is per file. The account's `REMOTE` record binds individual
files to daemons, managed by verbs:

```
> REMOTE-FILE SHARED /run/mvxd.sock      bind one file (own address)
> REMOTE-FILE HOT                        bind to the default $MVXDAEMON
> LIST-REMOTE
> LOCAL-FILE HOT                         unbind
```

Rules: an exact entry wins, a `*` entry binds every LMDB file, an
entry without an address uses `$MVXDAEMON`, and different files may
name different daemons. With a `REMOTE` record present, unlisted
files stay local — so one account can mix local LMDB, several
daemons, and directory files, and one program reads all of them
through ordinary `OPEN`/`READ`/`WRITE`. Binding affects resolution
only: existing data does not move when a binding changes (copy it
with a `SELECT`/`READ`/`WRITE` loop, or a future MIGRATE-FILE verb).

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
