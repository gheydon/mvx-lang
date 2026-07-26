/*
 * MVX — a native compiler and runtime for Pick/MultiValue BASIC.
 * Copyright (C) 2026 Gordon Heydon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.  There is NO WARRANTY, to
 * the extent permitted by law; see the LICENSE file for details.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* MVX storage driver contract.
 *
 * THE MINIMAL CONTRACT IS THE WHOLE CONTRACT (ARCHITECTURE.md 4.1).
 * Application code must run correctly against exactly this interface;
 * nothing above the driver may depend on backend-specific behaviour.
 * Records cross this boundary as MV dynamic strings — marshalling to
 * and from backend formats happens inside the driver.
 *
 * Record locks (READU semantics) deliberately do NOT appear here: they
 * live in the runtime's lock table, keyed by file spec and record id,
 * because a lock can be held across user think-time and must never pin
 * a backend transaction.
 */
#ifndef MVX_DRIVER_H
#define MVX_DRIVER_H

#include "mvx_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mvx_file mvx_file;       /* driver-owned handle */
typedef struct mvx_cursor mvx_cursor;   /* driver-owned select cursor */

/* One index maintenance operation: add or remove (key -> id) in the
   index named by item. */
typedef struct mvx_ixop {
    const char *item;
    const char *key;
    int64_t klen;
    int add;                            /* 1 = add entry, 0 = remove */
} mvx_ixop;

/* One column of a relational mapping (ARCHITECTURE / #18): the runtime
   computes the fields (backend-neutral) and the projected values; the
   driver renders them in its own form (SQL columns, document fields). */
typedef struct mvx_mapfield {
    const char *name;                   /* column / field name */
    const char *type;                   /* abstract type: TEXT/NUMERIC/... */
} mvx_mapfield;

/* One WITH condition for a multi-predicate push-down (select_multi). */
typedef struct mvx_pred {
    const char *col;                    /* mapped column, or NULL for the blob */
    int64_t attr;                       /* record attribute (when col NULL) */
    const char *op;                     /* "=", "#", ">", "<", ">=", "<=" */
    int numeric;                        /* compare numerically (range) */
    const char *val;
    int64_t vlen;
} mvx_pred;

typedef struct mvx_driver {
    const char *name;

    /* NULL on failure; err receives a message. */
    mvx_file *(*open)(const char *spec, char *err, size_t errlen);
    void (*close)(mvx_file *f);

    /* 1 = found (rec set), 0 = not found. */
    int (*read)(mvx_file *f, const char *id, int64_t idlen, mv_value *rec);

    /* 1 = ok, 0 = failure (bad id, backend error). */
    int (*write)(mvx_file *f, const char *id, int64_t idlen,
                 const mv_value *rec);

    /* 1 = deleted, 0 = did not exist. */
    int (*del)(mvx_file *f, const char *id, int64_t idlen);

    /* Snapshot the id list up front (short transaction), then stream. */
    mvx_cursor *(*select_begin)(mvx_file *f);
    int (*select_next)(mvx_cursor *c, mv_value *id);   /* 1 = got, 0 = end */
    void (*select_end)(mvx_cursor *c);

    /* File lifecycle: handle-less operations on the spec.  open() must
       NOT create — creation is explicit (the CREATE-FILE verb's
       primitive).  create returns 0 if the file already exists;
       remove returns 0 if it does not. */
    int (*create)(const char *spec, char *err, size_t errlen);
    int (*remove)(const char *spec, char *err, size_t errlen);

    /* Optional (may be NULL): enumerate the file specs this driver
       holds for the account, as an @AM-separated list.  Backing for
       the LISTF verb.  `loc` is the connection/location the runtime
       resolved for the account (e.g. "@conn" or a conninfo) so a
       backend that serves many locations knows which to list; a driver
       with a single implicit store (embedded LMDB, the daemon) ignores
       it.  DICT./index tables are included; the runtime filters them. */
    int (*names)(const char *loc, mv_value *out, char *err, size_t errlen);

    /* Optional indexing capability (ARCHITECTURE.md 5) — all four may
       be NULL if the backend has no native secondary indexes.  The
       record write/delete and its index deltas MUST commit atomically
       (one transaction): that is the no-index-drift guarantee.  Keys
       arrive already extracted; the driver stores (key -> record id)
       with duplicate keys allowed. */
    int (*write_ix)(mvx_file *f, const char *id, int64_t idlen,
                    const mv_value *rec, const mvx_ixop *ops, int nops);
    int (*del_ix)(mvx_file *f, const char *id, int64_t idlen,
                  const mvx_ixop *ops, int nops);
    /* NULL result = no such index (distinct from an empty cursor). */
    mvx_cursor *(*index_select)(mvx_file *f, const char *item,
                                const char *key, int64_t klen);
    int (*index_drop)(mvx_file *f, const char *item);

    /* Optional lock authority (may be NULL): a backend that arbitrates
       between sessions (the mvx-lmdbd daemon) grants and releases record
       locks itself; lock returns 0 while another session holds it.
       When NULL, the runtime's process-local lock table applies. */
    int (*lock)(mvx_file *f, const char *id, int64_t idlen);
    int (*unlock)(mvx_file *f, const char *id, int64_t idlen);

    /* Optional relational-mapping capability (may be NULL): project a
       mapped file's records into the backend's native relational form.
       The runtime supplies the columns and the per-record projected
       values; the driver materialises and persists them. */
    int (*map_ensure)(mvx_file *f, const mvx_mapfield *cols, int ncols,
                      char *err, size_t errlen);
    int (*map_apply)(mvx_file *f, const char *id, int64_t idlen,
                     const mvx_mapfield *cols, const char **vals,
                     const int64_t *vlens, int ncols);
    /* Association child tables (may be NULL): one table per association,
       rows keyed (id, seq).  map_child_apply replaces a record's rows;
       vals/vlens are row-major (nrows x ncols): cell (r,c) at r*ncols+c. */
    int (*map_child_ensure)(mvx_file *f, const char *assoc,
                            const mvx_mapfield *cols, int ncols,
                            char *err, size_t errlen);
    int (*map_child_apply)(mvx_file *f, const char *id, int64_t idlen,
                           const char *assoc, const mvx_mapfield *cols,
                           int ncols, const char **vals,
                           const int64_t *vlens, int nrows);
    /* Tear down a mapping: drop the parent columns and the association
       child tables (may be NULL). */
    int (*map_drop)(mvx_file *f, const mvx_mapfield *cols, int ncols,
                    const char **assocs, int nassocs, char *err,
                    size_t errlen);
    /* Native-read capability (may be NULL): read a mapped record back from
       the relational form, so external writes to the columns/child tables
       are visible.  map_read fills vals[i]/lens[i] for each parent column
       (vals[i] = NULL for SQL NULL); returns 1 if the parent row exists, 0
       if absent, -1 on error.  map_child_read allocates a row-major array
       of (*nrows * ncols) cells (and lens) for one association's rows,
       ordered by seq; returns 1 (including zero rows) or -1.  Every
       non-NULL string the driver returns is malloc'd; the runtime frees
       each cell, then *cells and *lens. */
    int (*map_read)(mvx_file *f, const char *id, int64_t idlen,
                    const mvx_mapfield *cols, int ncols,
                    char **vals, int64_t *lens);
    int (*map_child_read)(mvx_file *f, const char *id, int64_t idlen,
                          const char *assoc, const mvx_mapfield *cols,
                          int ncols, char ***cells, int64_t **lens,
                          int *nrows);
    /* Optional (may be NULL): build a native index for a mapped column in
       one shot — the backend already stores and maintains the column (via
       the mapping) and its own index, so there is no per-record backfill and
       no write_ix/del_ix maintenance.  A driver providing this advertises
       index_select/index_drop but leaves write_ix/del_ix NULL.  Returns the
       indexed row count, or -1 on error (e.g. the column is not mapped).
       `col` names a mapped identity column to index (NULL to index the raw
       record attribute `attr` via an expression index on the blob), so any
       dictionary field is indexable, not only mapped ones. */
    int (*index_create)(mvx_file *f, const char *item, const char *col,
                        int64_t attr);
    /* Optional server-side WITH push-down (may be NULL): the ids whose
       mapped column `col` satisfies `op` `val`, filtered in the backend so a
       query need not stream every record to the verb.  `op` is "=" or "#"
       (not-equal).  Uses the column's index if one exists, else a
       backend-side scan.  NULL result = cannot push down (caller scans). */
    mvx_cursor *(*select_where)(mvx_file *f, const char *col, const char *op,
                                const char *val, int64_t vlen);
    /* Optional server-side WITH push-down on a raw record attribute (may be
       NULL): the ids where attribute `attr` (1-based) of the stored record
       satisfies `op` `val`.  Unlike select_where this reads the attribute
       out of the record blob itself, so it needs no mapped column and is
       exact for any field type — at the cost of not using a column index.
       op is "=" or "#".  NULL result = cannot push down (caller scans). */
    mvx_cursor *(*select_attr)(mvx_file *f, int64_t attr, const char *op,
                               const char *val, int64_t vlen);
    /* Optional co-located TRANS() JOIN push-down (may be NULL): the source
       ids whose foreign key (source attribute `src_keyattr`) points at a
       target record whose attribute `tgt_attr` satisfies `op` `val`.  `tgt`
       is the opened target file; the driver runs the join only when it lives
       on the same backend/database as `src` (else NULL -> per-record TRANS).
       op is "=".  Turns an N+1 per-record lookup into one JOIN.
       src_keycol/tgt_col name a mapped identity column for the source key
       and the target attribute (NULL/"" to read it from the record blob);
       a real column can use an index and, in native mode, is authoritative. */
    mvx_cursor *(*select_join)(mvx_file *src, int64_t src_keyattr,
                               const char *src_keycol, mvx_file *tgt,
                               int64_t tgt_attr, const char *tgt_col,
                               const char *op, const char *val, int64_t vlen);
    /* Optional server-side COUNT (may be NULL): the number of matching
       records, computed in the backend so a filtered count returns one row
       instead of a stream of ids.  `op` NULL/"" counts all; otherwise it is
       "=" or "#" against a mapped identity column `col` (when set) or the raw
       record attribute `attr` (blob).  Returns the count, or -1 when it
       cannot push down (the caller counts by scanning). */
    int64_t (*count_where)(mvx_file *f, const char *col, int64_t attr,
                           const char *op, const char *val, int64_t vlen);
    /* Optional server-side SUM (may be NULL): the total of numeric column
       `sumcol`, written to `out` as text.  `fop` NULL/"" totals the whole
       file, else "="/"#" filters on a mapped identity column `fcol` (when
       set) or the raw record attribute `fattr`.  Returns 1 if pushed, 0 if
       not (the caller sums by scanning). */
    int (*sum_where)(mvx_file *f, const char *sumcol, const char *fcol,
                     int64_t fattr, const char *fop, const char *fval,
                     int64_t fvlen, char *out, size_t cap);
    /* Optional ORDER BY / LIMIT push-down (may be NULL): ids ordered by
       mapped column `ocol` (text ordered COLLATE "C" to match MV's byte sort
       when `otext`, else natural order), limited to `limit` rows (0 = all),
       optionally filtered like the other push-downs.  So a "top-N by field"
       fetches N ids server-side.  NULL result = cannot push (caller sorts). */
    mvx_cursor *(*select_order)(mvx_file *f, const char *fcol, int64_t fattr,
                                const char *fop, const char *fval,
                                int64_t fvlen, const char *ocol, int otext,
                                int64_t limit);
    /* Optional multi-condition WITH push-down (may be NULL): the ids matching
       every predicate (AND).  Each `mvx_pred` names a mapped column `col` or
       the raw record attribute `attr`, an op ("=","#",">","<",">=","<="), and
       whether to compare numerically.  NULL result = cannot push. */
    mvx_cursor *(*select_multi)(mvx_file *f, const mvx_pred *preds, int npred);
    /* Optional query-plan description (may be NULL): render — WITHOUT executing
       anything — how this backend would run a filtered/ordered id query.  For
       an SQL backend that is the SQL text; a document store would render its
       native query.  Inputs match select_multi (the AND'd predicates) plus an
       optional ORDER BY column (`otext` -> byte-order collation to match MV's
       sort) and LIMIT (0 = none).  Writes a NUL-terminated plan into `out`
       (truncated to `cap`); returns 1 if it rendered a server-side plan, 0 if
       it cannot (the caller then words the client-side fallback itself).  This
       is what backs the verbs' DESCRIBE modifier. */
    int (*explain)(mvx_file *f, const mvx_pred *preds, int npred,
                   const char *ocol, int otext, int64_t limit,
                   char *out, size_t cap);
} mvx_driver;

/* Common header every driver embeds first in its mvx_file. */
typedef struct mvx_file_base {
    const mvx_driver *driver;
    char *spec;                         /* canonical spec, lock-table key */
} mvx_file_base;

/* Drivers are shared libraries (libmvxdrv_<name>.dylib / .so), loaded
   with dlopen on first use.  Backend dependencies (liblmdb, a database
   client, ...) are linked into the driver library, so they load with
   the driver and never burden compiled programs that don't use them.

   Every driver library exports exactly one entry point:

       const mvx_driver *mvx_driver_entry(int abi);

   It must return NULL if `abi` is not an ABI version it supports,
   otherwise its driver vtable.  The search path is $MVXDRIVERS
   (colon-separated), then the runtime's built-in driver directory. */
#define MVX_DRIVER_ABI 6

typedef const mvx_driver *(*mvx_driver_entry_fn)(int abi);

#ifdef __cplusplus
}
#endif
#endif /* MVX_DRIVER_H */
