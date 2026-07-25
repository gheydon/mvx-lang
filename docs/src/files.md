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
attr 6: association name (optional — see Multivalues below)
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

### Multivalues and associations

An attribute may hold several values, separated by value marks (`@VM`).
`LIST` and `SORT` **explode** a multivalued attribute vertically — one
sub-row per value — rather than printing the marks. Single-valued
columns (and `@ID`) show on the record's first sub-row and stay blank on
the continuations, so each record reads as one block.

Related multivalues that vary together — an order's line items, say —
are declared an **association** by giving their dictionary items the same
name in **attribute 6**. Associated columns then align value-by-value,
their row count driven by the **controlling** member (the one with the
lowest attribute number):

```
PRODUCT:  D ^ 5 ^      ^ Product ^ 10L ^ ORDERITEMS
QTY:      D ^ 6 ^      ^ Qty     ^ 5R  ^ ORDERITEMS
PRICE:    D ^ 7 ^ MD2$ ^ Price   ^ 8R  ^ ORDERITEMS      (^ = @AM)
```
```
> LIST ORDERS CUSTOMER PRODUCT QTY PRICE
@ID          Customer     Product      Qty    Price
O1           Acme Corp    Widget         2    $9.99
                          Gadget         1    $4.50
O2           Beta Ltd     Sprocket       5    $1.25
```

Conversions and formats apply per value. Two *different* associations of
unequal length in one listing currently share a single explosion height
(the tallest column) rather than each exploding independently.

### Relational mapping (`MAP`)

Because the dictionary already knows which attributes are single-valued
and which belong to an association, it doubles as a **relational schema**.
`MAP file item…` prints the schema for the items you name — you generally
map just the fields you need — while `MAP file ALL` (or `*`) maps every
item. Single-valued attributes are parent-table columns, each association
is a child table keyed `(id, seq)`, and the column type comes from the
conversion (`MD…`→`NUMERIC`, `D…`→`DATE`, `MT…`→`TIME`, else `TEXT`).

```
> MAP ORDERS
CREATE TABLE ORDERS (
    id TEXT PRIMARY KEY
  , CUSTOMER TEXT
);

CREATE TABLE ORDERS_ORDERITEMS (
    id TEXT
  , seq INT
  , PRICE NUMERIC
  , PRODUCT TEXT
  , QTY TEXT
  , PRIMARY KEY (id, seq)
);
```

`MAP file DATA` also previews the projected rows (the record decomposed
into its parent row and child rows).

**`BUILD-MAP file field…`** materialises the mapping for real on a SQL
backend: the runtime computes the projection and the driver adds the
columns to the record's own table and backfills every record, so the raw
`rec` blob and the queryable columns sit side by side:

```
> BUILD-MAP CUST NAME CITY CREDIT
mapped 2 record(s) into 3 column(s)
```
```
 id | rec_bytes |   NAME    |   CITY    | CREDIT
----+-----------+-----------+-----------+--------
 C1 |        21 | Acme Corp | Sydney    | 15.00
```

Values are stored in their `OCONV` display form (`CREDIT` above is the
`MD2` conversion of the stored `1500`).

**Associations become child tables.** Map associated attributes and each
association gets its own table keyed `(id, seq)`, one row per value
position — the line items:

```
> BUILD-MAP ORDERS CUSTOMER PRODUCT QTY PRICE
ORDERS            (id, rec, CUSTOMER)
ORDERS_ORDERITEMS (id, seq, PRODUCT, QTY, PRICE)
  O1,1,Widget,2,$9.99 | O1,2,Gadget,1,$4.50 | O2,1,Sprocket,5,$1.25
```

This is the driver **mapping capability**: the runtime is backend-neutral
(it computes the columns, the child rows, and the projected values), and
each driver renders it in its own form — Postgres as parent columns +
child tables, a non-SQL backend as it sees fit (or not at all).

**`CREATE-MAP file field…`** declares the mapping — it writes a `%MAP%`
control record into the dictionary and builds it — after which the
projection is **kept live**: every `WRITE` to the file mirrors the record
into its columns and child rows automatically, no rebuild needed
(`BUILD-MAP` is the on-demand backfill). The projection is a derived
view, so a write is never blocked by it; the `rec` blob stays the source
of truth (`mirror` mode).

Columns are **typed** from the conversion: a masked-decimal item (`MD…`)
becomes a real `numeric` column, so `sum("PRICE")` works in SQL. The
value is reduced from its display form to a plain number (`$9.99` → the
numeric `9.99`); a value that isn't a number projects as `NULL` rather
than failing the write — the mirror-mode policy. Other items are `text`
for now; **`DATE`/`TIME`** typed columns and the `native` mode (backend
as source of truth) are the remaining phases of
[#18](https://github.com/mvx-lang/mvx/issues/18).

`LIST-MAPS` shows the account's mapped files and their fields, and
`DELETE-MAP file` tears a mapping down — dropping its columns and child
tables and removing `%MAP%`, so writes stop mirroring — the full lifecycle
alongside `CREATE-MAP`/`BUILD-MAP` (as `LIST-INDEXES`/`DELETE-INDEX` are
to indexing).

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
