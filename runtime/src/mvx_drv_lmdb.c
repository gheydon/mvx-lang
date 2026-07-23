/* Embedded LMDB driver: one environment per account at
 * <account>/mvxdata.lmdb, one named DB per MV file.  Key = record id,
 * value = MV record bytes — nearly a passthrough (ARCHITECTURE.md 4.2).
 *
 * Discipline enforced here:
 *   - short transactions only, one per operation;
 *   - reads copy out of the mmap before the txn ends;
 *   - single writer is LMDB's own constraint — nothing here assumes
 *     parallel writers;
 *   - key length validated against LMDB's 511-byte default.
 */
#include "mvx_driver.h"

#include <lmdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_KEY 511

typedef struct {
    mvx_file_base base;
    MDB_dbi dbi;
} lmdb_file;

struct mvx_cursor {
    mv_value *ids;
    int64_t n, pos;
};

/* One environment per process (per account).  Opened lazily. */
static MDB_env *g_env;

static MDB_env *env_get(char *err, size_t errlen) {
    if (g_env) return g_env;
    const char *acct = getenv("MVXACCOUNT");
    if (!acct || !acct[0]) acct = ".";
    char path[4096];
    snprintf(path, sizeof path, "%s/mvxdata.lmdb", acct);
    mkdir(path, 0775);

    MDB_env *env;
    int rc = mdb_env_create(&env);
    if (rc == 0) rc = mdb_env_set_maxdbs(env, 126);
    if (rc == 0) rc = mdb_env_set_mapsize(env, (size_t)1 << 30);
    if (rc == 0) rc = mdb_env_open(env, path, 0, 0664);
    if (rc != 0) {
        snprintf(err, errlen, "lmdb: %s: %s", path, mdb_strerror(rc));
        if (env) mdb_env_close(env);
        return NULL;
    }
    /* Reap reader-table slots left by killed processes; without this,
       enough unclean exits eventually exhaust the table and every
       open fails with MDB_READERS_FULL. */
    int dead = 0;
    mdb_reader_check(env, &dead);
    return g_env = env;
}

static const mvx_driver mvx_driver_lmdb;

/* Open the named DB; creating is the caller's choice via flags. */
static int dbi_open(unsigned flags, const char *spec, MDB_dbi *dbi,
                    char *err, size_t errlen) {
    MDB_env *env = env_get(err, errlen);
    if (!env) return 0;
    MDB_txn *txn;
    int rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc != 0) {
        snprintf(err, errlen, "lmdb: %s", mdb_strerror(rc));
        return 0;
    }
    rc = mdb_dbi_open(txn, spec, flags, dbi);
    if (rc != 0) {
        mdb_txn_abort(txn);
        if (rc != MDB_NOTFOUND)
            snprintf(err, errlen, "lmdb: %s: %s", spec, mdb_strerror(rc));
        return 0;
    }
    rc = mdb_txn_commit(txn);
    if (rc != 0) {
        snprintf(err, errlen, "lmdb: %s", mdb_strerror(rc));
        return 0;
    }
    return 1;
}

static mvx_file *lmdb_open(const char *spec, char *err, size_t errlen) {
    MDB_dbi dbi;
    if (!dbi_open(0, spec, &dbi, err, errlen))   /* no MDB_CREATE: explicit */
        return NULL;

    lmdb_file *f = calloc(1, sizeof(lmdb_file));
    if (!f) mvx_fatal("out of memory opening %s", spec);
    f->base.driver = &mvx_driver_lmdb;
    f->base.spec = strdup(spec);
    f->dbi = dbi;
    return (mvx_file *)f;
}

static void lmdb_close(mvx_file *fh) {
    lmdb_file *f = (lmdb_file *)fh;
    free(f->base.spec);
    free(f);
}

static int lmdb_read(mvx_file *fh, const char *id, int64_t idlen,
                     mv_value *rec) {
    lmdb_file *f = (lmdb_file *)fh;
    if (idlen > MAX_KEY) return 0;
    MDB_txn *txn;
    if (mdb_txn_begin(g_env, NULL, MDB_RDONLY, &txn) != 0) return 0;
    MDB_val k = {(size_t)idlen, (void *)id}, v;
    int rc = mdb_get(txn, f->dbi, &k, &v);
    if (rc == 0)
        mv_set_str(rec, v.mv_data, (int64_t)v.mv_size);  /* copy out */
    mdb_txn_abort(txn);
    return rc == 0;
}

static int lmdb_write(mvx_file *fh, const char *id, int64_t idlen,
                      const mv_value *rec) {
    lmdb_file *f = (lmdb_file *)fh;
    if (idlen > MAX_KEY) return 0;
    char nb[40];
    const char *rp;
    int64_t rlen = mv_val_chars(rec, nb, sizeof nb, &rp);

    MDB_txn *txn;
    if (mdb_txn_begin(g_env, NULL, 0, &txn) != 0) return 0;
    MDB_val k = {(size_t)idlen, (void *)id};
    MDB_val v = {(size_t)rlen, (void *)rp};
    int rc = mdb_put(txn, f->dbi, &k, &v, 0);
    if (rc != 0) { mdb_txn_abort(txn); return 0; }
    return mdb_txn_commit(txn) == 0;
}

static int lmdb_del(mvx_file *fh, const char *id, int64_t idlen) {
    lmdb_file *f = (lmdb_file *)fh;
    if (idlen > MAX_KEY) return 0;
    MDB_txn *txn;
    if (mdb_txn_begin(g_env, NULL, 0, &txn) != 0) return 0;
    MDB_val k = {(size_t)idlen, (void *)id};
    int rc = mdb_del(txn, f->dbi, &k, NULL);
    if (rc != 0) { mdb_txn_abort(txn); return 0; }
    return mdb_txn_commit(txn) == 0;
}

static mvx_cursor *lmdb_select_begin(mvx_file *fh) {
    lmdb_file *f = (lmdb_file *)fh;
    mvx_cursor *c = calloc(1, sizeof(mvx_cursor));
    if (!c) mvx_fatal("out of memory in SELECT");

    MDB_txn *txn;
    if (mdb_txn_begin(g_env, NULL, MDB_RDONLY, &txn) != 0) return c;
    MDB_cursor *cur;
    if (mdb_cursor_open(txn, f->dbi, &cur) != 0) {
        mdb_txn_abort(txn);
        return c;
    }
    MDB_val k, v;
    int64_t cap = 0;
    while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == 0) {
        if (c->n == cap) {
            cap = cap ? cap * 2 : 64;
            mv_value *ns = realloc(c->ids, (size_t)cap * sizeof(mv_value));
            if (!ns) mvx_fatal("out of memory in SELECT");
            c->ids = ns;
        }
        mv_init(&c->ids[c->n]);
        mv_set_str(&c->ids[c->n], k.mv_data, (int64_t)k.mv_size);
        c->n++;
    }
    mdb_cursor_close(cur);
    mdb_txn_abort(txn);                 /* snapshot done, txn closed */
    return c;
}

static int lmdb_select_next(mvx_cursor *c, mv_value *id) {
    if (c->pos >= c->n) return 0;
    mv_copy(id, &c->ids[c->pos++]);
    return 1;
}

static void lmdb_select_end(mvx_cursor *c) {
    for (int64_t i = 0; i < c->n; i++) mv_clear(&c->ids[i]);
    free(c->ids);
    free(c);
}

static int lmdb_create(const char *spec, char *err, size_t errlen);
static int lmdb_remove(const char *spec, char *err, size_t errlen);

static const mvx_driver mvx_driver_lmdb = {
    "lmdb",
    lmdb_open, lmdb_close,
    lmdb_read, lmdb_write, lmdb_del,
    lmdb_select_begin, lmdb_select_next, lmdb_select_end,
    lmdb_create, lmdb_remove,
};

static int lmdb_create(const char *spec, char *err, size_t errlen) {
    MDB_dbi dbi;
    if (dbi_open(0, spec, &dbi, err, errlen)) return 0;  /* already exists */
    return dbi_open(MDB_CREATE, spec, &dbi, err, errlen);
}

static int lmdb_remove(const char *spec, char *err, size_t errlen) {
    MDB_dbi dbi;
    if (!dbi_open(0, spec, &dbi, err, errlen)) return 0;
    MDB_txn *txn;
    if (mdb_txn_begin(g_env, NULL, 0, &txn) != 0) return 0;
    if (mdb_drop(txn, dbi, 1) != 0) { mdb_txn_abort(txn); return 0; }
    return mdb_txn_commit(txn) == 0;
}

const mvx_driver *mvx_driver_entry(int abi) {
    return abi == MVX_DRIVER_ABI ? &mvx_driver_lmdb : NULL;
}
