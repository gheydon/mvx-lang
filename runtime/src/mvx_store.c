/* Storage runtime: file-spec resolution, the runtime lock table, and the
 * active select list.  Drivers plug in beneath via mvx_driver.h.
 *
 * Resolution (account root = $MVXACCOUNT or cwd):
 *   - spec names an existing directory        -> directory driver
 *   - otherwise                               -> named DB in the account's
 *                                                LMDB environment
 */
#include "mvx_driver.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#ifdef __APPLE__
#define DRV_SUFFIX ".dylib"
#else
#define DRV_SUFFIX ".so"
#endif

#ifndef MVX_DRIVER_DIR
#define MVX_DRIVER_DIR "."
#endif

/* ------------------------------------------------ driver loading (dlopen) */

typedef struct loaded_drv {
    char *name;
    const mvx_driver *drv;
    struct loaded_drv *next;
} loaded_drv;

static loaded_drv *g_drivers;

static const mvx_driver *driver_try(const char *dir, const char *name) {
    char path[4096];
    snprintf(path, sizeof path, "%s/libmvxdrv_%s" DRV_SUFFIX, dir, name);
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) return NULL;
    mvx_driver_entry_fn entry =
        (mvx_driver_entry_fn)dlsym(h, "mvx_driver_entry");
    const mvx_driver *d = entry ? entry(MVX_DRIVER_ABI) : NULL;
    if (!d) {
        dlclose(h);
        mvx_fatal("storage driver %s is not compatible (ABI %d)", path,
                  MVX_DRIVER_ABI);
    }
    return d;                           /* handle stays open for process life */
}

/* Resolve a driver by name: $MVXDRIVERS (colon-separated), then the
   built-in driver directory.  A missing driver is a configuration
   error, not a missing file — fail loudly rather than taking ELSE. */
static const mvx_driver *driver_load(const char *name) {
    for (loaded_drv *l = g_drivers; l; l = l->next)
        if (strcmp(l->name, name) == 0) return l->drv;

    const mvx_driver *d = NULL;
    const char *sp = getenv("MVXDRIVERS");
    if (sp && sp[0]) {
        char *dup = strdup(sp);
        for (char *tok = strtok(dup, ":"); tok && !d;
             tok = strtok(NULL, ":"))
            d = driver_try(tok, name);
        free(dup);
    }
    if (!d) d = driver_try(MVX_DRIVER_DIR, name);
    if (!d)
        mvx_fatal("cannot load storage driver \"%s\" "
                  "(searched $MVXDRIVERS and %s)",
                  name, MVX_DRIVER_DIR);

    loaded_drv *l = malloc(sizeof(loaded_drv));
    if (!l) mvx_fatal("out of memory loading driver");
    l->name = strdup(name);
    l->drv = d;
    l->next = g_drivers;
    g_drivers = l;
    return d;
}

/* ------------------------------------------------------- per-ctx state */

typedef struct lock_ent {
    char *key;                          /* spec \x01 id */
    struct lock_ent *next;
} lock_ent;

typedef struct open_file {
    mvx_file *f;
    struct open_file *next;
} open_file;

typedef struct store_state {
    open_file *files;
    lock_ent *locks;
    mv_value *sel_ids;                  /* materialised select list */
    int64_t sel_n, sel_pos;
} store_state;

static store_state *state(mvx_ctx *ctx) {
    store_state *st = mvx_ctx_store_get(ctx);
    if (!st) {
        st = calloc(1, sizeof(store_state));
        if (!st) mvx_fatal("out of memory creating store state");
        mvx_ctx_store_set(ctx, st);
    }
    return st;
}

static void clear_select(store_state *st) {
    for (int64_t i = 0; i < st->sel_n; i++) mv_clear(&st->sel_ids[i]);
    free(st->sel_ids);
    st->sel_ids = NULL;
    st->sel_n = st->sel_pos = 0;
}

void mvx_store_shutdown(mvx_ctx *ctx) {
    store_state *st = mvx_ctx_store_get(ctx);
    if (!st) return;
    clear_select(st);
    for (lock_ent *l = st->locks; l;) {
        lock_ent *n = l->next;
        free(l->key);
        free(l);
        l = n;
    }
    for (open_file *o = st->files; o;) {
        open_file *n = o->next;
        mvx_file_base *b = (mvx_file_base *)o->f;
        b->driver->close(o->f);
        free(o);
        o = n;
    }
    free(st);
    mvx_ctx_store_set(ctx, NULL);
}

/* ------------------------------------------------------------- helpers */

static mvx_file *file_of(const mv_value *fvar, const char *what) {
    if (fvar->tag != MV_FILE || fvar->i == 0)
        mvx_fatal("%s: variable is not an open file variable", what);
    return (mvx_file *)(intptr_t)fvar->i;
}

static char *id_chars(const mv_value *id, char *buf, size_t cap,
                      int64_t *len) {
    const char *p;
    *len = mv_val_chars(id, buf, cap, &p);
    if (*len == 0) mvx_fatal("empty record id");
    return (char *)p;
}

static char *lock_key(mvx_file *f, const char *id, int64_t idlen) {
    mvx_file_base *b = (mvx_file_base *)f;
    size_t sl = strlen(b->spec);
    char *k = malloc(sl + 1 + (size_t)idlen + 1);
    if (!k) mvx_fatal("out of memory in lock table");
    memcpy(k, b->spec, sl);
    k[sl] = '\x01';
    memcpy(k + sl + 1, id, (size_t)idlen);
    k[sl + 1 + idlen] = '\0';
    return k;
}

static void lock_drop(store_state *st, const char *key) {
    for (lock_ent **pp = &st->locks; *pp; pp = &(*pp)->next) {
        if (strcmp((*pp)->key, key) == 0) {
            lock_ent *dead = *pp;
            *pp = dead->next;
            free(dead->key);
            free(dead);
            return;
        }
    }
}

/* ----------------------------------------------------------------- API */

/* Resolve a spec to its driver, and derive the dictionary spec when
   asked: DICT.<spec> as a sibling LMDB named DB, <spec>/.DICT as a
   hidden subdirectory for directory files (data SELECTs skip dotfiles,
   so the dictionary is invisible to them). */
static const mvx_driver *resolve(const char *cspec, int want_dict,
                                 char *outspec, size_t cap) {
    const char *acct = getenv("MVXACCOUNT");
    if (!acct || !acct[0]) acct = ".";

    char path[4096];
    if (cspec[0] == '/')
        snprintf(path, sizeof path, "%s", cspec);
    else
        snprintf(path, sizeof path, "%s/%s", acct, cspec);

    struct stat sb;
    if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        snprintf(outspec, cap, want_dict ? "%s/.DICT" : "%s", cspec);
        return driver_load("dir");
    }
    snprintf(outspec, cap, want_dict ? "DICT.%s" : "%s", cspec);
    return driver_load("lmdb");
}

int64_t mvx_open(mvx_ctx *ctx, const mv_value *dict, const mv_value *spec,
                 mv_value *fvar) {
    char db[40], sb[64];
    const char *dp = "";
    if (dict) mv_val_chars(dict, db, sizeof db, &dp);
    int want_dict = 0;
    if (dp[0] != '\0') {
        if (strcasecmp(dp, "DICT") == 0)
            want_dict = 1;
        else
            return 0;
    }

    const char *sp;
    int64_t slen = mv_val_chars(spec, sb, sizeof sb, &sp);
    if (slen == 0 || slen > 900 || memchr(sp, '\x01', (size_t)slen))
        return 0;

    char cspec[1024];
    memcpy(cspec, sp, (size_t)slen);
    cspec[slen] = '\0';

    char rspec[1152];
    const mvx_driver *drv = resolve(cspec, want_dict, rspec, sizeof rspec);

    char err[256] = "";
    mvx_file *f = drv->open(rspec, err, sizeof err);
    if (!f) return 0;

    store_state *st = state(ctx);
    open_file *o = malloc(sizeof(open_file));
    if (!o) mvx_fatal("out of memory in OPEN");
    o->f = f;
    o->next = st->files;
    st->files = o;

    mv_clear(fvar);
    fvar->tag = MV_FILE;
    fvar->i = (int64_t)(intptr_t)f;
    return 1;
}

int64_t mvx_read(mvx_ctx *ctx, mv_value *rec, const mv_value *fvar,
                 const mv_value *id, int64_t lock) {
    mvx_file *f = file_of(fvar, "READ");
    mvx_file_base *b = (mvx_file_base *)f;
    char ib[40];
    int64_t idlen;
    const char *ip = id_chars(id, ib, sizeof ib, &idlen);

    if (lock) {
        store_state *st = state(ctx);
        char *key = lock_key(f, ip, idlen);
        lock_drop(st, key);             /* re-lock by same session is fine */
        lock_ent *l = malloc(sizeof(lock_ent));
        if (!l) mvx_fatal("out of memory in lock table");
        l->key = key;
        l->next = st->locks;
        st->locks = l;
    }
    return b->driver->read(f, ip, idlen, rec);
}

void mvx_write(mvx_ctx *ctx, const mv_value *rec, const mv_value *fvar,
               const mv_value *id, int64_t keep_lock) {
    mvx_file *f = file_of(fvar, "WRITE");
    mvx_file_base *b = (mvx_file_base *)f;
    char ib[40];
    int64_t idlen;
    const char *ip = id_chars(id, ib, sizeof ib, &idlen);
    if (!b->driver->write(f, ip, idlen, rec))
        mvx_fatal("WRITE failed on %s id %.*s", b->spec, (int)idlen, ip);
    if (!keep_lock) {                   /* WRITE releases; WRITEU keeps */
        char *key = lock_key(f, ip, idlen);
        lock_drop(state(ctx), key);
        free(key);
    }
}

int64_t mvx_delete_rec(mvx_ctx *ctx, const mv_value *fvar,
                       const mv_value *id) {
    mvx_file *f = file_of(fvar, "DELETE");
    mvx_file_base *b = (mvx_file_base *)f;
    char ib[40];
    int64_t idlen;
    const char *ip = id_chars(id, ib, sizeof ib, &idlen);
    int64_t r = b->driver->del(f, ip, idlen);
    char *key = lock_key(f, ip, idlen);
    lock_drop(state(ctx), key);
    free(key);
    return r;
}

void mvx_release(mvx_ctx *ctx, const mv_value *fvar, const mv_value *id) {
    store_state *st = state(ctx);
    if (!fvar) {                        /* bare RELEASE: drop everything */
        for (lock_ent *l = st->locks; l;) {
            lock_ent *n = l->next;
            free(l->key);
            free(l);
            l = n;
        }
        st->locks = NULL;
        return;
    }
    mvx_file *f = file_of(fvar, "RELEASE");
    char ib[40];
    int64_t idlen;
    const char *ip = id_chars(id, ib, sizeof ib, &idlen);
    char *key = lock_key(f, ip, idlen);
    lock_drop(st, key);
    free(key);
}

void mvx_select(mvx_ctx *ctx, const mv_value *fvar) {
    mvx_file *f = file_of(fvar, "SELECT");
    mvx_file_base *b = (mvx_file_base *)f;
    store_state *st = state(ctx);
    clear_select(st);

    /* Materialise inside one short driver transaction — matches MV
       select-list semantics and keeps read txns short (4.2). */
    mvx_cursor *c = b->driver->select_begin(f);
    if (!c) return;
    int64_t cap = 0;
    mv_value id;
    mv_init(&id);
    while (b->driver->select_next(c, &id)) {
        if (st->sel_n == cap) {
            cap = cap ? cap * 2 : 64;
            mv_value *ns = realloc(st->sel_ids,
                                   (size_t)cap * sizeof(mv_value));
            if (!ns) mvx_fatal("out of memory in SELECT");
            st->sel_ids = ns;
        }
        mv_init(&st->sel_ids[st->sel_n]);
        mv_copy(&st->sel_ids[st->sel_n], &id);
        st->sel_n++;
    }
    mv_clear(&id);
    b->driver->select_end(c);
}

int64_t mvx_readnext(mvx_ctx *ctx, mv_value *id) {
    store_state *st = state(ctx);
    if (st->sel_pos >= st->sel_n) return 0;
    mv_copy(id, &st->sel_ids[st->sel_pos++]);
    return 1;
}

/* -------------------------------------------------------- file lifecycle
   The primitives behind the future CREATE-FILE / DELETE-FILE verbs
   (which will be BASIC programs, per the architecture). */

static int spec_cstr(const mv_value *spec, char *out, size_t cap) {
    char nb[40];
    const char *sp;
    int64_t slen = mv_val_chars(spec, nb, sizeof nb, &sp);
    if (slen == 0 || (size_t)slen >= cap) return 0;
    memcpy(out, sp, (size_t)slen);
    out[slen] = '\0';
    return 1;
}

int64_t mvx_createfile(mvx_ctx *ctx, const mv_value *spec,
                       const mv_value *type) {
    (void)ctx;
    char cspec[1024];
    if (!spec_cstr(spec, cspec, sizeof cspec)) return 0;

    const char *drvname = "lmdb";
    char tb[40];
    const char *tp = "";
    if (type) mv_val_chars(type, tb, sizeof tb, &tp);
    if (tp[0] == 'D' || tp[0] == 'd') drvname = "dir";

    const mvx_driver *drv = driver_load(drvname);
    char err[256] = "";
    if (!drv->create(cspec, err, sizeof err)) return 0;

    /* CREATE-FILE creates DICT and DATA together, classic style. */
    char dspec[1152];
    if (strcmp(drvname, "dir") == 0)
        snprintf(dspec, sizeof dspec, "%s/.DICT", cspec);
    else
        snprintf(dspec, sizeof dspec, "DICT.%s", cspec);
    if (!drv->create(dspec, err, sizeof err)) {
        drv->remove(cspec, err, sizeof err);
        return 0;
    }
    return 1;
}

int64_t mvx_deletefile(mvx_ctx *ctx, const mv_value *spec) {
    (void)ctx;
    char cspec[1024];
    if (!spec_cstr(spec, cspec, sizeof cspec)) return 0;

    char dspec[1152], rspec[1152];
    const mvx_driver *drv = resolve(cspec, 1, dspec, sizeof dspec);
    char err[256] = "";
    drv->remove(dspec, err, sizeof err);        /* dict first, may be absent */
    resolve(cspec, 0, rspec, sizeof rspec);
    return drv->remove(rspec, err, sizeof err);
}
