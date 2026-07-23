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
} mvx_driver;

/* Common header every driver embeds first in its mvx_file. */
typedef struct mvx_file_base {
    const mvx_driver *driver;
    char *spec;                         /* canonical spec, lock-table key */
} mvx_file_base;

extern const mvx_driver mvx_driver_lmdb;
extern const mvx_driver mvx_driver_dir;

#ifdef __cplusplus
}
#endif
#endif /* MVX_DRIVER_H */
