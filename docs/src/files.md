# Files, Dictionaries and Indexes

## Files and backends

An MV file is a keyed record store. The **account** is a directory;
within it a file is either:

- a **directory file** — a subdirectory, one record per Unix file,
  attributes as lines. Perfect for source (`BP`) and anything git
  should own natively; or
- an **LMDB file** — a named database inside the account's
  `mvxdata.lmdb` environment (or the daemon's — see deployment).

`OPEN` decides by looking: a directory of that name means the
directory driver, otherwise LMDB (local, or daemon-backed if the file
is bound — see Deployment). Creation is explicit — `OPEN` of a
nonexistent file takes the ELSE branch; `CREATE-FILE name {DIR |
REMOTE {addr}}` chooses the type once, and makes the dictionary at the
same time.

## Dictionaries

Every file has a dictionary: a sibling store (`DICT.X` for LMDB,
`X/.DICT` for directory files) opened with `OPEN "DICT", "X" TO D`.
Dictionary records describe fields:

**D-type** (attribute) items:

```
attr 1: D
attr 2: attribute number
attr 3: conversion (an OCONV code, e.g. MD2$ or D2/)
attr 4: column heading
attr 5: format, e.g. 12L or 8R
```

**I-type** (computed) items put an expression in attribute 2 instead
of a number. The first computed function is `DOCTAG(tag)`, which
scans a record's comment lines for a docblock annotation `@tag value`
— so with `FILE` and `VERSION` items on `BP`:

```
> LIST BP FILE VERSION WITH VERSION = 1.2
```

reads program metadata straight out of source docblocks.

`LIST` and `SELECT` drive columns, filters (`WITH`), and ordering
(`BY` — numeric when the item's format is right-justified) entirely
from the dictionary.

## Record locks

`READU` locks a record until `WRITE`, `DELETE`, or `RELEASE`
(`WRITEU` writes while keeping the lock). Locks are per session. With
the daemon, the daemon is the lock authority and locks are leased to
the connection: a client that dies loses its locks immediately.

## Secondary indexes

```
> CREATE-INDEX PARTS COLOR
index PARTS.COLOR built, 3 record(s)
> LIST PARTS NAME WITH COLOR = blue      served by the index
```

Indexes are maintained in the write path: every `WRITE`/`DELETE`
updates only the entries whose values changed, and the record and its
index entries commit in one transaction — no drift. Multivalued
attributes index one entry per value. Only D-type items are indexable:
a computed item could depend on data outside the record and go
silently stale (the classic `TRANS()` problem), so `CREATE-INDEX`
refuses. `LIST`/`SELECT` use an index automatically for equality
`WITH` filters and fall back to scanning.

Metadata lives in the dictionary record `%INDEXES%`; manage with
`CREATE-INDEX`, `DELETE-INDEX`, `LIST-INDEXES`, or the
`INDEXBUILD`/`INDEXDROP`/`INDEXSELECT` intrinsics.
