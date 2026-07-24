# Files, Dictionaries and Indexes

## Files and backends

An MV file is a keyed record store. The **account** is a directory;
within it a file is either:

- a **directory file** — a subdirectory, one record per Unix file,
  attributes as lines. Perfect for source (`BP`) and anything git
  should own natively; or
- an **LMDB file** — a named database inside the account's
  `mvxdata.lmdb` environment (or the daemon's — see deployment).

An account directory carries a `.mvx` descriptor (`name`, `version`)
that marks it as an MVX account and names it — the reliable marker
because the VOC may live inside the LMDB environment or on a daemon,
not as a file. It is written when the account is created and used for
the session prompt.

`OPEN` decides by looking: a directory of that name means the
directory driver, otherwise LMDB (local, or daemon-backed if the file
is bound — see Deployment). Creation is explicit — `OPEN` of a
nonexistent file takes the ELSE branch; `CREATE-FILE name {DIR |
USING driver {conn}}` chooses the backend once, and makes the dictionary at the
same time.

## Dictionaries

Every file has a dictionary: a sibling store (`DICT.X` for LMDB, or the
directory `X.DICT` beside `X` for directory files — so `BP` and
`BP.DICT` sit side by side) opened with `OPEN "DICT", "X" TO D`.
Dictionary records describe fields:

**D-type** (attribute) items:

```
attr 1: D
attr 2: attribute number
attr 3: conversion (an OCONV code, e.g. MD2$ or D2/)
attr 4: column heading
attr 5: format, e.g. 12L or 8R
```

**Control records** (ids starting with `%`) hold file metadata, not
fields, and are hidden from `LIST DICT`. `CREATE-FILE` stamps `%FILE%`
(attribute 1 `FILE`, then the backend type and connection) into every
new file's dictionary, and `CREATE-INDEX` maintains `%INDEXES%`.
Because they live in the dictionary, they travel with it in git — so a
clone knows a file's backend and indexes without a separate manifest
(see Version Control / `BUILD`).

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

A locking read (`READU`, `READVU`, `MATREADU`) may carry a **`LOCKED`**
clause, which runs when another session already holds the record — the
read then neither blocks nor takes the lock:

```
READU R FROM F, ID LOCKED
   PRINT "busy, try later"
END THEN
   ... got the record and the lock ...
END ELSE
   ... no such record ...
END
```

Without a `LOCKED` clause a locking read blocks until the holder
releases (classic `READU`). `LOCKED` only fires against the daemon, the
cross-session lock authority; a purely local file has no other session
to contend with, so the clause is inert there.

## ON ERROR

A file statement may carry an **`ON ERROR`** clause that runs when the
backend rejects the operation, instead of aborting the program (classic
MV drops to the debugger on a fatal file fault):

```
WRITE REC ON F, ID ON ERROR
   PRINT "could not write ":ID
END
```

`ON ERROR` comes after the operands, and on a read before `LOCKED` and
`THEN`/`ELSE`. It is honoured by the write path — `WRITE`, `WRITEV`,
`MATWRITE` — where a driver can refuse the write (for instance the
directory driver rejecting a record id that is not a valid file name).
It parses on `READ`/`DELETE` too, for source portability, but the read
and delete paths surface no recoverable fault today, so the clause is
inert there. Without `ON ERROR`, a fatal file fault aborts the program
as before.

## READV / WRITEV

`READV var FROM fvar, id, n` reads just attribute `n` of a record (1-based,
in `@FM` order); an attribute past the end of the record comes back empty,
and a missing record takes the `ELSE`. `WRITEV expr ON fvar, id, n`
replaces attribute `n` and leaves the rest of the record untouched,
extending the record with empty attributes if `n` is past the end and
creating the record if it does not yet exist. The `U` variants
(`READVU`/`WRITEVU`) take and hold a record lock, as with `READU`/`WRITEU`.

## MATREAD / MATWRITE

`MATREAD arr FROM fvar, id` reads a record and spreads its fields (`@FM`
separated) across a `DIM`'d array in storage order — element 1 the first
field, and so on. If the record has more fields than the array holds, the
**last element absorbs the remainder**, field marks and all; if it has
fewer, the trailing elements come back empty. A missing record takes the
`ELSE` and leaves the array untouched. `MATREADU` adds a record lock.

`MATWRITE arr ON fvar, id` is the inverse: it joins the elements with
`@FM` into one record and writes it, then **strips trailing attribute
marks** — so trailing empty elements disappear, and so do any marks left
at the end of the last element (for instance one that absorbed overflow on
a prior `MATREAD`). `MATWRITEU` keeps the lock.

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
