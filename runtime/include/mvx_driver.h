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
       the LISTF verb. */
    int (*names)(mv_value *out, char *err, size_t errlen);

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
       between sessions (the mvxd daemon) grants and releases record
       locks itself; lock returns 0 while another session holds it.
       When NULL, the runtime's process-local lock table applies. */
    int (*lock)(mvx_file *f, const char *id, int64_t idlen);
    int (*unlock)(mvx_file *f, const char *id, int64_t idlen);
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
#define MVX_DRIVER_ABI 4

typedef const mvx_driver *(*mvx_driver_entry_fn)(int abi);

#ifdef __cplusplus
}
#endif
#endif /* MVX_DRIVER_H */
