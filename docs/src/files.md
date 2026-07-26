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

**`TRANS(file,keyattr,attr,control)`** is a computed item that follows a
foreign key: it reads attribute `keyattr` of the record as a key into
`file` and returns that record's attribute `attr` (`attr` 0 is the key
itself). A missing target yields the empty string, or the key when
`control` is `C`. So an `ORDERS` file that keeps a customer id can show and
filter the customer's own fields:

```
> LIST ORDERS PRODUCT CUSTNAME CUSTCITY WITH CUSTCITY = "Sydney"
```

where `CUSTNAME` is `TRANS(CUSTOMERS,1,1,X)`. The same lookup is available to
programs as the `TRANS(file,key,attr,control)` / `XLATE(...)` function.

Per record this is a foreign-key lookup — read the source's key, read the
target row. On its own that is the N+1 problem: a lookup for every source
record. So when the source and target files are **co-located on the same SQL
backend**, a `WITH` filter on a `TRANS` item pushes down to a single `JOIN`:

```
> LIST ORDERS PRODUCT CITY WITH CITY = "Sydney"
```
```sql
SELECT s.id FROM orders s JOIN customers t
    ON convert_from(t.id,'LATIN1')
     = split_part(convert_from(s.rec,'LATIN1'), chr(254), 1)   -- the FK
 WHERE split_part(convert_from(t.rec,'LATIN1'), chr(254), 2) = $1  -- CITY
```

The whole filter runs in the backend and only matching ids come back — no
per-record probing. It works purely off the record blobs, so neither file
needs mapped columns, and joins on the target's primary key. The push-down
covers `=` with the default `X` control (the inner-join case that exactly
matches the reference); anything else — a different backend for the target,
`#`, control `C` — falls back to the per-record lookup, so the result is
never wrong. (For a networked key-value target the equivalent optimisation
is a batched multi-get rather than a join.)

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

A Pick `WRITE` hands the runtime the whole record, but the projection is
columns and rows — so an update writes **only what changed**. The runtime
diffs the record against its prior version (the same prior-record read that
maintains secondary indexes) and updates just the parent columns whose
attribute moved, skipping the column `UPDATE` entirely when none did, and
re-writing an association's child rows only when one of its attributes
changed. Editing one field of a record with a large association is one
small `UPDATE`, not a re-`DELETE`/`INSERT` of every line item.

Columns are **typed** from the conversion: a masked-decimal item (`MD…`)
becomes a real `numeric` column, so `sum("PRICE")` works in SQL. The
value is reduced from its display form to a plain number (`$9.99` → the
numeric `9.99`); a value that isn't a number projects as `NULL` rather
than failing the write — the mirror-mode policy.

Date (`D…`) and time (`MT…`) items likewise become real `date` and `time`
columns. These project from the **stored internal value** — the Pick day
count and seconds-past-midnight — rendered straight to ISO-8601
(`2026-07-25`, `14:30:00`), not the locale-shaped display conversion,
which a backend cannot parse unambiguously. An empty cell projects as
`NULL`. So `WHEN >= DATE '2026-01-01'` and `date_trunc('month', "WHEN")`
work in SQL while the record still reads through its `D4/` conversion in
BASIC. Other items are `text`.

**Mirror vs native mode.** A mapping has a write policy, shown and changed
with `MAP-MODE file {native|mirror}` and defaulting to `mirror`:

- **mirror** — the record blob is the source of truth; the projection is a
  derived, best-effort view. A value that does not fit its typed column
  (letters in a `numeric`, a non-date in a `date`) is stored as `NULL` and
  the `WRITE` still succeeds. A projection is never allowed to block a
  write.
- **native** — the typed columns are authoritative. A `WRITE` whose value
  does not fit its column is *rejected before it commits*: the statement
  takes its `ON ERROR` path (or aborts, as classic MV does for an unhandled
  write failure) and the record is left unwritten. And a `READ` recomposes
  the record *from* the columns and child rows, so a change made straight
  to the SQL — by another application, a reporting tool, an `UPDATE` at the
  console — is what the program reads back:

  - single-valued attributes come from the parent columns, associations
    from their child tables (ordered by `seq`), each reverse-converted from
    the stored form to the internal value (`ICONV`; ISO date/time parsed
    back to the day/second count);
  - **un-mapped attributes are preserved** — the `rec` blob is the base and
    only the mapped attributes are overlaid, so it doubles as the carrier
    for everything the schema doesn't describe;
  - a record that exists **only in SQL** (an external `INSERT`, never
    written through MVX) is recomposed from an empty base, so `READ`,
    `LIST`, and `COUNT` all see it.

  This is how you make the relational tables the system of record while
  MVX programs keep reading and writing normally.

```
> MAP-MODE ITEM native
ITEM mapping mode: native
```
```
WRITE "Widget":@AM:"1000" ON F, "I1" ON ERROR PRINT "rejected"   ;* ok
WRITE "Broken":@AM:"abc"  ON F, "I2" ON ERROR PRINT "rejected"   ;* rejected
```

Switching an existing mapping to native first checks every record against
the schema (via `MAPCHECK`) and refuses the switch if any record would
violate it, naming the count — so you never enter native mode with data
the schema would reject. This completes the mapping epic
[#18](https://github.com/mvx-lang/mvx/issues/18).

`LIST-MAPS` shows the account's mapped files with their mode, state, and
fields, and `DELETE-MAP file` tears a mapping down — dropping its columns
and child tables and removing `%MAP%`, so writes stop mirroring — the full
lifecycle alongside `CREATE-MAP`/`BUILD-MAP` (as `LIST-INDEXES`/
`DELETE-INDEX` are to indexing).

```
> LIST-MAPS
ORDERS           native   current  CUSTOMER ORDER_DATE TOTAL PRODUCT QTY
1 mapping(s)
```

A mapping is a **snapshot** of the dictionary taken at `CREATE-MAP` time —
the projection reads that `%MAP%` snapshot, not the live dict — so editing a
mapped dictionary item (its conversion, attribute number, association, or
deleting it) does **not** re-project or re-type the columns on its own; the
mapping and the dictionary silently diverge. The **state** column surfaces
this: `current` when every mapped field still matches its dictionary item,
`stale` when one has drifted (or its dict item is gone). A stale mapping is
brought back in line by rebuilding it — `DELETE-MAP` then `CREATE-MAP` —
which drops and recreates the columns/child tables with the current types.

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

**On a SQL backend the filter runs server-side.** An equality/not-equal
`WITH` filter is pushed into the backend instead of streaming every record to
the verb and filtering in BASIC — only the matching ids come back. On a large
table that is the difference between reading the matches and reading the whole
file. There are two push-down paths, in order of preference:

- **Mapped identity column** — a `TEXT` column with no conversion (names,
  codes, states) holds exactly the raw attribute, so the query runs
  `SELECT id ... WHERE col = $1`, and `CREATE-INDEX` makes that an indexed
  lookup (a real `CREATE INDEX` on the column; the backend maintains it, no
  backfill).
- **Any other field — straight off the record blob.** An un-mapped field, or
  a converted one (numeric, date), is read out of the stored record with
  `split_part(convert_from(rec,'LATIN1'), chr(254), N)` — attribute *N*
  between the field marks. Comparing that raw attribute to the raw `WITH`
  value is exact for every field type, and needs no column, though it cannot
  use a column index. So even a filter on a field you never mapped filters in
  Postgres rather than in the verb.

Push-down covers `=` and `#` (not-equal, which includes empty/absent
attributes, matching MV) with a non-empty value; range operators and
computed (`I`-type) items still fall back to the record scan, so the result
is never wrong — they just aren't accelerated yet. Editing the tables
directly keeps everything correct, since the columns and indexes belong to
the database.
