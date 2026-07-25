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

/* Storage runtime: file-spec resolution, the runtime lock table, and the
 * active select list.  Drivers plug in beneath via mvx_driver.h.
 *
 * Resolution (account root = $MVXACCOUNT or cwd):
 *   - spec names an existing directory        -> directory driver
 *   - otherwise                               -> named DB in the account's
 *                                                LMDB environment
 */
#include "mvx_driver.h"

#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

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
    /* Drivers ship in libmvxrt's own directory: relocatable, no baked
       path.  The compile-time dir is the last-resort fallback. */
    const char *rtd = mvx_runtime_dir();
    if (!d && rtd[0]) d = driver_try(rtd, name);
    if (!d) d = driver_try(MVX_DRIVER_DIR, name);
    if (!d)
        mvx_fatal("cannot load storage driver \"%s\" "
                  "(searched $MVXDRIVERS, %s and %s)",
                  name, rtd[0] ? rtd : "(runtime dir)", MVX_DRIVER_DIR);

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
    mvx_file *f;                        /* for driver-held locks */
    struct lock_ent *next;
} lock_ent;

#define IX_MAX_ITEMS 16
#define IX_MAX_VALS 32
#define IX_KEY_MAX 511

typedef struct ixmeta {
    int loaded;
    int n;
    struct {
        char item[128];
        int64_t attr;
    } it[IX_MAX_ITEMS];
} ixmeta;

/* Relational mapping declared for a file (%MAP%), cached per open file so
   writes can be mirrored into the projection (#18). */
#define MAP_MAXF 64
#define MAP_MAXA 16
typedef struct mapmeta {
    int loaded;
    int ensured;                        /* schema materialised this session */
    int nf;
    char *buf;                          /* owns the parsed field strings */
    char *names[MAP_MAXF], *convs[MAP_MAXF], *types[MAP_MAXF];
    char *assocs[MAP_MAXF];
    int64_t anos[MAP_MAXF];
} mapmeta;

typedef struct open_file {
    mvx_file *f;
    ixmeta ix;
    mapmeta map;
    struct open_file *next;
} open_file;

/* mapping helpers, defined below but used by the WRITE mirror hook */
static void map_load(open_file *o);
static int map_project_one(mvx_ctx *ctx, mvx_file *f, mapmeta *m,
                           const char *id, int64_t idlen,
                           const mv_value *rec);

typedef struct store_state {
    open_file *files;
    lock_ent *locks;
    mv_value *sel_ids;                  /* materialised select list */
    int64_t sel_n, sel_pos;
    int sel_active;                     /* a list was formed this process */
} store_state;

static void sel_push(store_state *st, int64_t *cap, const char *p,
                     int64_t len) {
    if (st->sel_n == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        mv_value *ns = realloc(st->sel_ids,
                               (size_t)*cap * sizeof(mv_value));
        if (!ns) mvx_fatal("out of memory in select list");
        st->sel_ids = ns;
    }
    mv_init(&st->sel_ids[st->sel_n]);
    mv_set_str(&st->sel_ids[st->sel_n], p, len);
    st->sel_n++;
}

/* --------------------------------------------------- session select list
   The session/select-list seam (ARCHITECTURE.md 7.3): a program ending
   with an unconsumed select list persists the remainder to the session
   file ($MVXSESSION, owned by the TCL session); the next program's
   first READNEXT picks it up.  That is how "SELECT ..." feeds the next
   command, classic style, across processes.  Ids are newline-separated
   (record ids containing newlines are not supported here).           */

static void session_load(store_state *st) {
    if (st->sel_active) return;
    const char *sf = getenv("MVXSESSION");
    if (!sf || !sf[0]) return;
    FILE *fp = fopen(sf, "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); return; }
    char *buf = malloc((size_t)sz);
    if (!buf) mvx_fatal("out of memory loading select list");
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        free(buf);
        return;
    }
    fclose(fp);
    fclose(fopen(sf, "wb"));            /* consumed exactly once */

    st->sel_active = 1;
    int64_t cap = 0;
    const char *p = buf, *end = buf + sz;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        int64_t len = (nl ? nl : end) - p;
        if (len > 0) sel_push(st, &cap, p, len);
        p = nl ? nl + 1 : end;
    }
    free(buf);
}

static void session_save(store_state *st) {
    if (!st->sel_active || st->sel_pos >= st->sel_n) return;
    const char *sf = getenv("MVXSESSION");
    if (!sf || !sf[0]) return;
    FILE *fp = fopen(sf, "wb");
    if (!fp) return;
    for (int64_t i = st->sel_pos; i < st->sel_n; i++) {
        char nb[40];
        const char *p;
        int64_t len = mv_val_chars(&st->sel_ids[i], nb, sizeof nb, &p);
        fwrite(p, 1, (size_t)len, fp);
        fputc('\n', fp);
    }
    fclose(fp);
}

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
    session_save(st);                   /* hand leftover list to session */
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

/* Drop a lock entry; when the backend is the lock authority, tell it. */
static void lock_drop(store_state *st, const char *key) {
    for (lock_ent **pp = &st->locks; *pp; pp = &(*pp)->next) {
        if (strcmp((*pp)->key, key) == 0) {
            lock_ent *dead = *pp;
            *pp = dead->next;
            if (dead->f) {
                mvx_file_base *b = (mvx_file_base *)dead->f;
                if (b->driver->unlock) {
                    const char *id = strchr(dead->key, '\x01') + 1;
                    b->driver->unlock(dead->f, id,
                                      (int64_t)strlen(id));
                }
            }
            free(dead->key);
            free(dead);
            return;
        }
    }
}

/* ----------------------------------------------------------------- API */

/* The account's default namespace on a daemon: the basename of the
   resolved MVXACCOUNT path (so ".", "./acct", and "/x/acct" all agree),
   or "default".  Shared by the store and the lmdbnet driver so a
   whole-account binding and its LISTF land in the same namespace. */
void mvx_account_namespace(char *out, size_t outlen) {
    const char *a = getenv("MVXACCOUNT");
    if (!a || !a[0]) a = ".";
    char rp[4096];
    const char *path = realpath(a, rp) ? rp : a;
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') n--;
    size_t s = n;
    while (s > 0 && path[s - 1] != '/') s--;
    size_t len = n - s;
    if (len == 0 || (len == 1 && path[s] == '.')) {
        snprintf(out, outlen, "default");
        return;
    }
    if (len >= outlen) len = outlen - 1;
    memcpy(out, path + s, len);
    out[len] = '\0';
}

/* Does this file have a backend binding?  Consult the account's
   BINDINGS record, whose lines are "SPEC driver {params...}" (an exact
   spec, or "*" for every LMDB file); driver names a storage driver
   (lmdbnet, and later postgres, mongo, ...) and params is its
   connection string, opaque to the runtime.  With no BINDINGS record,
   bare $MVXDAEMON binds the whole account to lmdbnet.  Returns 1 when
   bound, filling driver and params. */
static int binding_for(const char *cspec, char *driver, size_t dcap,
                       char *params, size_t pcap) {
    const char *envd = getenv("MVXDAEMON");
    const char *acct = getenv("MVXACCOUNT");
    if (!acct || !acct[0]) acct = ".";
    char path[4096];
    snprintf(path, sizeof path, "%s/BINDINGS", acct);
    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (envd && envd[0]) {
            char nsb[128];
            mvx_account_namespace(nsb, sizeof nsb);
            snprintf(driver, dcap, "lmdbnet");
            snprintf(params, pcap, "%s %s", envd, nsb);
            return 1;
        }
        return 0;
    }
    int star = 0, exact = 0;
    char stardrv[64] = "", starparm[512] = "";
    char exactdrv[64] = "", exactparm[512] = "";
    char ln[1152];
    while (fgets(ln, sizeof ln, fp)) {
        char *p = ln;
        while (*p == ' ' || *p == '\t') p++;
        char *sp = p;
        while (*sp && *sp != ' ' && *sp != '\t' && *sp != '\n' &&
               *sp != '\r')
            sp++;
        size_t nl = (size_t)(sp - p);
        if (nl == 0 || *p == '#') continue;
        char *dp = sp;                  /* driver field */
        while (*dp == ' ' || *dp == '\t') dp++;
        char *de = dp;
        while (*de && *de != ' ' && *de != '\t' && *de != '\n' &&
               *de != '\r')
            de++;
        char *pp = de;                  /* params: rest of the line */
        while (*pp == ' ' || *pp == '\t') pp++;
        char *pe = pp;
        while (*pe && *pe != '\n' && *pe != '\r') pe++;
        char *drvbuf, *parmbuf;
        size_t dl, pl;
        if (nl == 1 && p[0] == '*') {
            star = 1; drvbuf = stardrv; parmbuf = starparm;
            dl = sizeof stardrv; pl = sizeof starparm;
        } else if (strlen(cspec) == nl && memcmp(p, cspec, nl) == 0) {
            exact = 1; drvbuf = exactdrv; parmbuf = exactparm;
            dl = sizeof exactdrv; pl = sizeof exactparm;
        } else {
            continue;
        }
        snprintf(drvbuf, dl, "%.*s", (int)(de - dp), dp);
        snprintf(parmbuf, pl, "%.*s", (int)(pe - pp), pp);
    }
    fclose(fp);
    if (!exact && !star) return 0;
    const char *ud = exact ? exactdrv : stardrv;
    const char *up = exact ? exactparm : starparm;
    /* `@name` binds to a named connection profile: the driver comes from
       the profile, and the "@name" reference passes through so the
       driver resolves the address/namespace/token itself. */
    if (ud[0] == '@') {
        const char *cn = ud + 1;
        static char cdriver[64];
        if (!mvx_conn_lookup(cn, "driver", cdriver, sizeof cdriver))
            mvx_fatal("file %s: connection '%s' is not defined "
                      "(SET-CONNECTION %s driver=...)", cspec, cn, cn);
        snprintf(driver, dcap, "%s", cdriver);
        snprintf(params, pcap, "@%s", cn);
        return 1;
    }
    if (!ud[0]) ud = "lmdbnet";         /* default backend */
    if (!up[0] && strcmp(ud, "lmdbnet") == 0)
        up = envd && envd[0] ? envd : "";
    if (strcmp(ud, "lmdbnet") == 0 && !up[0])
        mvx_fatal("file %s is bound to lmdbnet but no daemon address "
                  "is configured (BINDINGS line or $MVXDAEMON)", cspec);
    snprintf(driver, dcap, "%s", ud);
    /* lmdbnet params are "addr [namespace]"; supply the account's
       default namespace when the binding names only an address. */
    static char nsparm[640];
    if (strcmp(ud, "lmdbnet") == 0) {
        const char *sp = up;
        while (*sp && *sp != ' ' && *sp != '\t') sp++;
        while (*sp == ' ' || *sp == '\t') sp++;
        if (!*sp) {
            char nsb[128];
            mvx_account_namespace(nsb, sizeof nsb);
            snprintf(nsparm, sizeof nsparm, "%s %s", up, nsb);
            up = nsparm;
        }
    }
    snprintf(params, pcap, "%s", up);
    return 1;
}

/* Resolve a spec to its driver, and derive the dictionary spec when
   asked: DICT.<spec> as a sibling LMDB named DB, and <spec>.DICT as a
   sibling directory for directory files — so a directory file NAME and
   its dictionary NAME.DICT sit side by side (BP and BP.DICT), matching
   the git-legible form exactly. */
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
        snprintf(outspec, cap, want_dict ? "%s.DICT" : "%s", cspec);
        return driver_load("dir");
    }
    /* Per-file backend binding (ARCHITECTURE.md 4.4: migration is per
       file).  A file may be bound to a networked or foreign backend by
       the account's BINDINGS record; unbound LMDB files are local.
       The connection params ride in the driver-level spec as
       "params\nspec" - opaque to the runtime, parsed by the driver. */
    char driver[64], params[512];
    if (binding_for(cspec, driver, sizeof driver, params, sizeof params)) {
        snprintf(outspec, cap, want_dict ? "%s\nDICT.%s" : "%s\n%s",
                 params, cspec);
        return driver_load(driver);
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
    if (!f) {
        /* A plain missing file is the normal ELSE path and stays
           silent; an infrastructure failure must say why. */
        if (err[0])
            fprintf(stderr, "OPEN %s: %s\n", cspec, err);
        return 0;
    }

    store_state *st = state(ctx);
    /* calloc, not malloc: the index metadata (o->ix) must start zeroed,
       or ix_load reads a garbage "loaded" flag and skips initialising it,
       leaving ix_diff to walk a garbage index count. */
    open_file *o = calloc(1, sizeof(open_file));
    if (!o) mvx_fatal("out of memory in OPEN");
    o->f = f;
    o->next = st->files;
    st->files = o;

    mv_clear(fvar);
    fvar->tag = MV_FILE;
    fvar->i = (int64_t)(intptr_t)f;
    return 1;
}

/* ------------------------------------------------------------ indexing
   Metadata: the DICT record %INDEXES% lists indexed item names, one
   per attribute; each item's dictionary record supplies the attribute
   number.  Cached per open file; CREATE-INDEX / DELETE-INDEX
   invalidate through mvx_index_build / mvx_index_drop.  Maintenance
   is diff-based: only entries whose values actually changed are
   touched (ARCHITECTURE.md 5.3). */

static open_file *find_open(store_state *st, mvx_file *f) {
    for (open_file *o = st->files; o; o = o->next)
        if (o->f == f) return o;
    return NULL;
}

static void ix_load(open_file *o) {
    if (o->ix.loaded) return;
    o->ix.loaded = 1;
    o->ix.n = 0;
    mvx_file_base *b = (mvx_file_base *)o->f;
    if (!b->driver->write_ix) return;   /* backend has no capability */

    char dspec[1720];
    const char *nl = strchr(b->spec, '\n');
    if (nl)
        snprintf(dspec, sizeof dspec, "%.*s\nDICT.%s",
                 (int)(nl - b->spec), b->spec, nl + 1);
    else
        snprintf(dspec, sizeof dspec, "DICT.%s", b->spec);
    char err[256] = "";
    mvx_file *d = b->driver->open(dspec, err, sizeof err);
    if (!d) return;

    mv_value xl, item, drec, ano;
    mv_init(&xl); mv_init(&item); mv_init(&drec); mv_init(&ano);
    if (b->driver->read(d, "%INDEXES%", 9, &xl)) {
        char nb[40];
        const char *p;
        int64_t len = mv_val_chars(&xl, nb, sizeof nb, &p);
        int64_t nattr = len ? 1 : 0;
        for (int64_t i = 0; i < len; i++)
            if (p[i] == '\xFE') nattr++;
        for (int64_t a = 1; a <= nattr && o->ix.n < IX_MAX_ITEMS; a++) {
            mv_extract_fn(&item, &xl, a, 0, 0);
            char ib[40];
            const char *ip;
            int64_t il = mv_val_chars(&item, ib, sizeof ib, &ip);
            if (il <= 0 || il >= 127) continue;
            char iname[128];
            memcpy(iname, ip, (size_t)il);
            iname[il] = '\0';
            if (!b->driver->read(d, iname, il, &drec)) continue;
            mv_extract_fn(&ano, &drec, 2, 0, 0);
            int64_t attr = mv_get_int(&ano);
            if (attr < 1) continue;
            memcpy(o->ix.it[o->ix.n].item, iname, (size_t)il + 1);
            o->ix.it[o->ix.n].attr = attr;
            o->ix.n++;
        }
    }
    mv_clear(&xl); mv_clear(&item); mv_clear(&drec); mv_clear(&ano);
    b->driver->close(d);
}

/* Values of one attribute, split on value marks; empty values and keys
   over the backend limit are not indexed. */
typedef struct ixvals {
    int n;
    char v[IX_MAX_VALS][IX_KEY_MAX + 1];
    int64_t len[IX_MAX_VALS];
} ixvals;

static void ix_values(const mv_value *rec, int64_t attr, ixvals *out) {
    out->n = 0;
    mv_value a;
    mv_init(&a);
    mv_extract_fn(&a, rec, attr, 0, 0);
    char nb[40];
    const char *p;
    int64_t len = mv_val_chars(&a, nb, sizeof nb, &p);
    const char *end = p + len;
    while (p < end && out->n < IX_MAX_VALS) {
        const char *vm = memchr(p, '\xFD', (size_t)(end - p));
        int64_t n = (vm ? vm : end) - p;
        if (n > 0 && n <= IX_KEY_MAX) {
            memcpy(out->v[out->n], p, (size_t)n);
            out->v[out->n][n] = '\0';
            out->len[out->n] = n;
            out->n++;
        }
        p = vm ? vm + 1 : end;
    }
    mv_clear(&a);
}

static int ixvals_has(const ixvals *vs, const char *p, int64_t n) {
    for (int i = 0; i < vs->n; i++)
        if (vs->len[i] == n && memcmp(vs->v[i], p, (size_t)n) == 0)
            return 1;
    return 0;
}

/* Build the delta list for one record transition old -> new. */
static int ix_diff(open_file *o, const mv_value *oldrec, int hadold,
                   const mv_value *newrec, mvx_ixop *ops, ixvals *pool) {
    int nops = 0;
    for (int k = 0; k < o->ix.n; k++) {
        ixvals *ov = &pool[k * 2], *nv = &pool[k * 2 + 1];
        ov->n = 0;
        if (hadold) ix_values(oldrec, o->ix.it[k].attr, ov);
        nv->n = 0;
        if (newrec) ix_values(newrec, o->ix.it[k].attr, nv);
        for (int i = 0; i < ov->n; i++)
            if (!ixvals_has(nv, ov->v[i], ov->len[i])) {
                ops[nops].item = o->ix.it[k].item;
                ops[nops].key = ov->v[i];
                ops[nops].klen = ov->len[i];
                ops[nops].add = 0;
                nops++;
            }
        for (int i = 0; i < nv->n; i++)
            if (!ixvals_has(ov, nv->v[i], nv->len[i])) {
                ops[nops].item = o->ix.it[k].item;
                ops[nops].key = nv->v[i];
                ops[nops].klen = nv->len[i];
                ops[nops].add = 1;
                nops++;
            }
    }
    return nops;
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
        if (b->driver->lock) {
            if (lock == 2) {
                /* READU ... LOCKED: try once; on contention report -1
                   without reading or taking the lock. */
                if (!b->driver->lock(f, ip, idlen)) {
                    free(key);
                    return -1;
                }
            } else {
                /* Backend is the lock authority: classic READU blocks
                   until the holder releases (or its connection drops). */
                while (!b->driver->lock(f, ip, idlen)) usleep(50000);
            }
        }
        lock_ent *l = malloc(sizeof(lock_ent));
        if (!l) mvx_fatal("out of memory in lock table");
        l->key = key;
        l->f = b->driver->lock ? f : NULL;
        l->next = st->locks;
        st->locks = l;
    }
    return b->driver->read(f, ip, idlen, rec);
}

/* Returns 0 on success, or -2 on a backend write failure when the caller
   has an ON ERROR handler (onerr != 0); without a handler a failure is
   fatal, as classic MV aborts to the debugger.  On error the record is
   not written and the lock is left as it was. */
int64_t mvx_write(mvx_ctx *ctx, const mv_value *rec, const mv_value *fvar,
                  const mv_value *id, int64_t keep_lock, int64_t onerr) {
    mvx_file *f = file_of(fvar, "WRITE");
    mvx_file_base *b = (mvx_file_base *)f;
    store_state *st = state(ctx);
    char ib[40];
    int64_t idlen;
    const char *ip = id_chars(id, ib, sizeof ib, &idlen);

    open_file *o = find_open(st, f);
    int ok;
    if (o) ix_load(o);
    if (o && o->ix.n > 0) {
        mv_value old;
        mv_init(&old);
        int had = b->driver->read(f, ip, idlen, &old);
        mvx_ixop ops[IX_MAX_ITEMS * IX_MAX_VALS * 2];
        static ixvals pool[IX_MAX_ITEMS * 2];
        int nops = ix_diff(o, &old, had, rec, ops, pool);
        ok = b->driver->write_ix(f, ip, idlen, rec, ops, nops);
        mv_clear(&old);
    } else {
        ok = b->driver->write(f, ip, idlen, rec);
    }
    if (!ok) {
        if (onerr) return -2;
        mvx_fatal("WRITE failed on %s id %.*s", b->spec, (int)idlen, ip);
    }
    /* mirror-on-write: keep a declared relational mapping (%MAP%) current.
       Best-effort — the record is the source of truth, so a projection
       failure never fails the write. */
    if (o) {
        map_load(o);
        if (o->map.nf > 0)
            map_project_one(ctx, f, &o->map, ip, idlen, rec);
    }
    if (!keep_lock) {                   /* WRITE releases; WRITEU keeps */
        char *key = lock_key(f, ip, idlen);
        lock_drop(st, key);
        free(key);
    }
    return 0;
}

/* MATREAD arr FROM file, id: read the record and distribute its fields
   across the dimensioned array.  Returns found (drives THEN/ELSE); the
   array is left untouched when the record is absent. */
int64_t mvx_matread(mvx_ctx *ctx, mv_array *arr, const mv_value *fvar,
                    const mv_value *id, int64_t lock) {
    mv_value rec;
    mv_init(&rec);
    int64_t found = mvx_read(ctx, &rec, fvar, id, lock);
    if (found > 0) mv_matparse(arr, &rec);
    mv_clear(&rec);
    return found;
}

/* MATWRITE arr ON file, id: join the array into a record and write it. */
int64_t mvx_matwrite(mvx_ctx *ctx, const mv_array *arr, const mv_value *fvar,
                     const mv_value *id, int64_t keep_lock, int64_t onerr) {
    mv_value rec;
    mv_init(&rec);
    mv_matbuild(arr, &rec);
    int64_t st = mvx_write(ctx, &rec, fvar, id, keep_lock, onerr);
    mv_clear(&rec);
    return st;
}

/* READV var FROM file, id, attr: read one attribute of a record.
   Returns found (drives THEN/ELSE); target untouched when absent. */
int64_t mvx_readv(mvx_ctx *ctx, mv_value *dst, const mv_value *fvar,
                  const mv_value *id, int64_t attr, int64_t lock) {
    mv_value rec;
    mv_init(&rec);
    int64_t found = mvx_read(ctx, &rec, fvar, id, lock);
    if (found > 0) mv_extract_fn(dst, &rec, attr, 0, 0);
    mv_clear(&rec);
    return found;
}

/* WRITEV expr ON file, id, attr: replace one attribute, preserving the
   rest of the record (creating it if absent). */
int64_t mvx_writev(mvx_ctx *ctx, const mv_value *val, const mv_value *fvar,
                   const mv_value *id, int64_t attr, int64_t keep_lock,
                   int64_t onerr) {
    mv_value rec;
    mv_init(&rec);
    if (!mvx_read(ctx, &rec, fvar, id, 0)) mv_set_str(&rec, "", 0);
    mv_replace_fn(&rec, &rec, attr, 0, 0, val);
    int64_t st = mvx_write(ctx, &rec, fvar, id, keep_lock, onerr);
    mv_clear(&rec);
    return st;
}

int64_t mvx_delete_rec(mvx_ctx *ctx, const mv_value *fvar,
                       const mv_value *id) {
    mvx_file *f = file_of(fvar, "DELETE");
    mvx_file_base *b = (mvx_file_base *)f;
    store_state *st = state(ctx);
    char ib[40];
    int64_t idlen;
    const char *ip = id_chars(id, ib, sizeof ib, &idlen);

    open_file *o = find_open(st, f);
    int64_t r;
    if (o) ix_load(o);
    if (o && o->ix.n > 0) {
        mv_value old;
        mv_init(&old);
        int had = b->driver->read(f, ip, idlen, &old);
        if (had) {
            mvx_ixop ops[IX_MAX_ITEMS * IX_MAX_VALS * 2];
            static ixvals pool[IX_MAX_ITEMS * 2];
            int nops = ix_diff(o, &old, 1, NULL, ops, pool);
            r = b->driver->del_ix(f, ip, idlen, ops, nops);
        } else {
            r = 0;
        }
        mv_clear(&old);
    } else {
        r = b->driver->del(f, ip, idlen);
    }
    char *key = lock_key(f, ip, idlen);
    lock_drop(st, key);
    free(key);
    return r;
}

/* ------------------------------------------- index verbs' entry points */

int64_t mvx_index_build(mvx_ctx *ctx, const mv_value *fvar,
                        const mv_value *item) {
    mvx_file *f = file_of(fvar, "INDEXBUILD");
    mvx_file_base *b = (mvx_file_base *)f;
    if (!b->driver->write_ix) return -1;
    open_file *o = find_open(state(ctx), f);
    if (!o) return -1;

    o->ix.loaded = 0;                   /* pick up the new %INDEXES% */
    ix_load(o);

    char nb[40];
    const char *ip;
    int64_t il = mv_val_chars(item, nb, sizeof nb, &ip);
    char iname[128];
    if (il <= 0 || il >= 127) return -1;
    memcpy(iname, ip, (size_t)il);
    iname[il] = '\0';
    int slot = -1;
    for (int k = 0; k < o->ix.n; k++)
        if (strcmp(o->ix.it[k].item, iname) == 0) slot = k;
    if (slot < 0) return -1;

    b->driver->index_drop(f, iname);    /* rebuild from empty */

    mvx_cursor *c = b->driver->select_begin(f);
    if (!c) return -1;
    int64_t count = 0;
    mv_value rid, rec;
    mv_init(&rid); mv_init(&rec);
    while (b->driver->select_next(c, &rid)) {
        char rb[40];
        const char *rp;
        int64_t rl = mv_val_chars(&rid, rb, sizeof rb, &rp);
        if (!b->driver->read(f, rp, rl, &rec)) continue;
        ixvals vs;
        ix_values(&rec, o->ix.it[slot].attr, &vs);
        mvx_ixop ops[IX_MAX_VALS];
        for (int i = 0; i < vs.n; i++) {
            ops[i].item = iname;
            ops[i].key = vs.v[i];
            ops[i].klen = vs.len[i];
            ops[i].add = 1;
        }
        if (!b->driver->write_ix(f, rp, rl, &rec, ops, vs.n)) {
            b->driver->select_end(c);
            mv_clear(&rid); mv_clear(&rec);
            return -1;
        }
        count++;
    }
    b->driver->select_end(c);
    mv_clear(&rid); mv_clear(&rec);
    return count;
}

/* --- relational mapping (see MAP / #18) -------------------------------
   Build a mapped file's projection.  spec is an @AM list, one field per
   element as "name <VM> attr# <VM> conv <VM> type <VM> assoc" (empty
   assoc = a single-valued parent column; otherwise a member of that
   association's child table).  The runtime decomposes each record; the
   driver materialises columns / child tables and persists.  Returns the
   record count, -1 on error, or -2 when the backend has no mapping. */

/* Extract attribute `ano` value `seq` (seq 0 = whole attribute), OCONV by
   `conv` if set, into a NUL-terminated scratch cell; returns its length. */
static int64_t map_cell(mvx_ctx *ctx, const mv_value *rec, int64_t ano,
                        int64_t seq, const char *conv, mv_value *av,
                        mv_value *ov, mv_value *code, char *dst,
                        size_t cap) {
    mv_extract_fn(av, rec, ano, seq, 0);
    const mv_value *src = av;
    if (conv[0]) {
        mv_set_str(code, conv, (int64_t)strlen(conv));
        mv_oconv(ctx, ov, av, code);
        src = ov;
    }
    char tb[40];
    const char *tp;
    int64_t tl = mv_val_chars(src, tb, sizeof tb, &tp);
    if (tl >= (int64_t)cap) tl = (int64_t)cap - 1;
    memcpy(dst, tp, (size_t)tl);
    dst[tl] = '\0';
    return tl;
}

/* Number of values in an attribute (0 if empty). */
static int map_vcount(const mv_value *rec, int64_t ano, mv_value *av) {
    mv_extract_fn(av, rec, ano, 0, 0);
    char tb[40];
    const char *tp;
    int64_t tl = mv_val_chars(av, tb, sizeof tb, &tp);
    if (tl == 0) return 0;
    int n = 1;
    for (int64_t x = 0; x < tl; x++)
        if (tp[x] == (char)0xFD) n++;
    return n;
}

/* Parse a %MAP% / spec string (@AM fields, each
   "name<VM>attr<VM>conv<VM>type<VM>assoc") into m, which owns the text. */
static void map_parse(const char *sp, int64_t slen, mapmeta *m) {
    m->nf = 0;
    m->buf = malloc((size_t)slen + 1);
    if (!m->buf) return;
    memcpy(m->buf, sp, (size_t)slen);
    m->buf[slen] = '\0';
    char *p = m->buf;
    while (m->nf < MAP_MAXF && *p) {
        char *amend = p;
        while (*amend && *amend != (char)0xFE) amend++;
        char save = *amend;
        *amend = '\0';
        char *parts[5] = {p, NULL, NULL, NULL, NULL};
        int np = 0;
        for (char *q = p; *q; q++)
            if (*q == (char)0xFD) { *q = '\0'; if (++np < 5) parts[np] = q + 1; }
        int i = m->nf;
        m->names[i] = parts[0];
        m->anos[i] = parts[1] ? strtoll(parts[1], NULL, 10) : 0;
        m->convs[i] = parts[2] ? parts[2] : (char *)"";
        m->types[i] = parts[3] ? parts[3] : (char *)"TEXT";
        m->assocs[i] = parts[4] ? parts[4] : (char *)"";
        m->nf++;
        if (save == 0) break;
        p = amend + 1;
    }
}

/* Group m's fields into parent columns + per-association members and
   materialise the schema via the driver.  Returns 1/0. */
static int map_ensure_schema(mvx_file *f, mapmeta *m, char *err,
                             size_t errlen) {
    mvx_file_base *b = (mvx_file_base *)f;
    mvx_mapfield pcol[MAP_MAXF];
    int npar = 0;
    for (int i = 0; i < m->nf; i++)
        if (m->assocs[i][0] == '\0') {
            pcol[npar].name = m->names[i];
            pcol[npar].type = m->types[i];
            npar++;
        }
    if (npar > 0 && !b->driver->map_ensure(f, pcol, npar, err, errlen))
        return 0;
    char *an[MAP_MAXA];
    int am[MAP_MAXA][MAP_MAXF], anm[MAP_MAXA], na = 0;
    for (int i = 0; i < m->nf; i++) {
        if (m->assocs[i][0] == '\0') continue;
        int slot = -1;
        for (int a = 0; a < na; a++)
            if (strcmp(an[a], m->assocs[i]) == 0) slot = a;
        if (slot < 0 && na < MAP_MAXA) {
            slot = na; an[na] = m->assocs[i]; anm[na++] = 0;
        }
        if (slot >= 0 && anm[slot] < MAP_MAXF) am[slot][anm[slot]++] = i;
    }
    if (na > 0 &&
        (!b->driver->map_child_ensure || !b->driver->map_child_apply)) {
        snprintf(err, errlen, "backend has no association child tables");
        return 0;
    }
    for (int a = 0; a < na; a++) {
        mvx_mapfield cc[MAP_MAXF];
        for (int k = 0; k < anm[a]; k++) {
            int i = am[a][k];
            cc[k].name = m->names[i];
            cc[k].type = m->types[i];
        }
        if (!b->driver->map_child_ensure(f, an[a], cc, anm[a], err, errlen))
            return 0;
    }
    return 1;
}

/* Project one record into the mapped columns / child tables. */
static int map_project_one(mvx_ctx *ctx, mvx_file *f, mapmeta *m,
                           const char *id, int64_t idlen,
                           const mv_value *rec) {
    mvx_file_base *b = (mvx_file_base *)f;
    mv_value av, ov, code;
    mv_init(&av); mv_init(&ov); mv_init(&code);
    int ok = 1;

    mvx_mapfield pcol[MAP_MAXF];
    int pidx[MAP_MAXF], npar = 0;
    for (int i = 0; i < m->nf; i++)
        if (m->assocs[i][0] == '\0') {
            pcol[npar].name = m->names[i];
            pcol[npar].type = m->types[i];
            pidx[npar++] = i;
        }
    if (npar > 0) {
        static char ps[MAP_MAXF][256];
        const char *vals[MAP_MAXF];
        int64_t vlens[MAP_MAXF];
        for (int k = 0; k < npar; k++) {
            int i = pidx[k];
            vlens[k] = map_cell(ctx, rec, m->anos[i], 0, m->convs[i], &av,
                                &ov, &code, ps[k], sizeof ps[0]);
            vals[k] = ps[k];
        }
        if (!b->driver->map_apply(f, id, idlen, pcol, vals, vlens, npar))
            ok = 0;
    }

    if (ok && b->driver->map_child_apply) {
        char *an[MAP_MAXA];
        int am[MAP_MAXA][MAP_MAXF], anm[MAP_MAXA], na = 0;
        for (int i = 0; i < m->nf; i++) {
            if (m->assocs[i][0] == '\0') continue;
            int slot = -1;
            for (int a = 0; a < na; a++)
                if (strcmp(an[a], m->assocs[i]) == 0) slot = a;
            if (slot < 0 && na < MAP_MAXA) {
                slot = na; an[na] = m->assocs[i]; anm[na++] = 0;
            }
            if (slot >= 0 && anm[slot] < MAP_MAXF) am[slot][anm[slot]++] = i;
        }
        for (int a = 0; a < na && ok; a++) {
            int nm = anm[a];
            mvx_mapfield cc[MAP_MAXF];
            for (int k = 0; k < nm; k++) {
                int i = am[a][k];
                cc[k].name = m->names[i];
                cc[k].type = m->types[i];
            }
            int nv = 0;
            for (int k = 0; k < nm; k++) {
                int vc = map_vcount(rec, m->anos[am[a][k]], &av);
                if (vc > nv) nv = vc;
            }
            if (nv == 0) {
                b->driver->map_child_apply(f, id, idlen, an[a], cc, nm,
                                           NULL, NULL, 0);
                continue;
            }
            size_t ncell = (size_t)nv * (size_t)nm;
            const char **cv = malloc(ncell * sizeof *cv);
            int64_t *cl = malloc(ncell * sizeof *cl);
            char *arena = malloc(ncell * 256);
            if (!cv || !cl || !arena) {
                free(cv); free(cl); free(arena);
                ok = 0;
                break;
            }
            for (int seq = 1; seq <= nv; seq++)
                for (int k = 0; k < nm; k++) {
                    int i = am[a][k];
                    size_t cell = (size_t)(seq - 1) * nm + k;
                    char *dst = arena + cell * 256;
                    cl[cell] = map_cell(ctx, rec, m->anos[i], seq,
                                        m->convs[i], &av, &ov, &code, dst,
                                        256);
                    cv[cell] = dst;
                }
            if (!b->driver->map_child_apply(f, id, idlen, an[a], cc, nm, cv,
                                            cl, nv))
                ok = 0;
            free(cv); free(cl); free(arena);
        }
    }
    mv_clear(&av); mv_clear(&ov); mv_clear(&code);
    return ok;
}

/* Load a file's declared mapping (%MAP% in its dictionary) into o->map,
   once per open, and materialise its schema.  With no %MAP% the file is
   simply not mapped. */
static void map_load(open_file *o) {
    if (o->map.loaded) return;
    o->map.loaded = 1;
    o->map.nf = 0;
    mvx_file_base *b = (mvx_file_base *)o->f;
    if (!b->driver->map_ensure || !b->driver->map_apply) return;
    char dspec[1720];
    const char *nl = strchr(b->spec, '\n');
    if (nl)
        snprintf(dspec, sizeof dspec, "%.*s\nDICT.%s",
                 (int)(nl - b->spec), b->spec, nl + 1);
    else
        snprintf(dspec, sizeof dspec, "DICT.%s", b->spec);
    char err[256] = "";
    mvx_file *d = b->driver->open(dspec, err, sizeof err);
    if (!d) return;
    mv_value mp;
    mv_init(&mp);
    if (b->driver->read(d, "%MAP%", 5, &mp)) {
        char nb[40];
        const char *sp;
        int64_t sl = mv_val_chars(&mp, nb, sizeof nb, &sp);
        if (sl > 0) {
            map_parse(sp, sl, &o->map);
            if (o->map.nf > 0 &&
                map_ensure_schema(o->f, &o->map, err, sizeof err))
                o->map.ensured = 1;
        }
    }
    mv_clear(&mp);
    b->driver->close(d);
}

int64_t mvx_mapbuild(mvx_ctx *ctx, const mv_value *fvar,
                     const mv_value *spec) {
    mvx_file *f = file_of(fvar, "MAPBUILD");
    mvx_file_base *b = (mvx_file_base *)f;
    if (!b->driver->map_ensure || !b->driver->map_apply) return -2;

    char nb[40];
    const char *sp;
    int64_t slen = mv_val_chars(spec, nb, sizeof nb, &sp);
    mapmeta m;
    memset(&m, 0, sizeof m);
    map_parse(sp, slen, &m);
    if (m.nf == 0) { free(m.buf); return 0; }

    char err[256] = "";
    if (!map_ensure_schema(f, &m, err, sizeof err)) {
        if (err[0]) fprintf(stderr, "MAPBUILD: %s\n", err);
        free(m.buf);
        return -1;
    }

    mvx_cursor *c = b->driver->select_begin(f);
    if (!c) { free(m.buf); return -1; }
    int64_t count = 0, rc = 0;
    mv_value rid, rec;
    mv_init(&rid); mv_init(&rec);
    while (b->driver->select_next(c, &rid)) {
        char rb[40];
        const char *rp;
        int64_t rl = mv_val_chars(&rid, rb, sizeof rb, &rp);
        if (!b->driver->read(f, rp, rl, &rec)) continue;
        if (!map_project_one(ctx, f, &m, rp, rl, &rec)) { rc = -1; break; }
        count++;
    }
    b->driver->select_end(c);
    mv_clear(&rid); mv_clear(&rec);
    free(m.buf);
    return rc < 0 ? rc : count;
}

int64_t mvx_index_drop(mvx_ctx *ctx, const mv_value *fvar,
                       const mv_value *item) {
    mvx_file *f = file_of(fvar, "INDEXDROP");
    mvx_file_base *b = (mvx_file_base *)f;
    if (!b->driver->index_drop) return 0;
    char nb[40];
    const char *ip;
    int64_t il = mv_val_chars(item, nb, sizeof nb, &ip);
    char iname[128];
    if (il <= 0 || il >= 127) return 0;
    memcpy(iname, ip, (size_t)il);
    iname[il] = '\0';
    int64_t r = b->driver->index_drop(f, iname);
    open_file *o = find_open(state(ctx), f);
    if (o) o->ix.loaded = 0;            /* metadata changed */
    return r;
}

int64_t mvx_index_select(mvx_ctx *ctx, const mv_value *fvar,
                         const mv_value *item, const mv_value *key) {
    mvx_file *f = file_of(fvar, "INDEXSELECT");
    mvx_file_base *b = (mvx_file_base *)f;
    if (!b->driver->index_select) return 0;
    open_file *o = find_open(state(ctx), f);
    if (!o) return 0;
    ix_load(o);

    char nb[40];
    const char *ip;
    int64_t il = mv_val_chars(item, nb, sizeof nb, &ip);
    char iname[128];
    if (il <= 0 || il >= 127) return 0;
    memcpy(iname, ip, (size_t)il);
    iname[il] = '\0';
    int found = 0;
    for (int k = 0; k < o->ix.n; k++)
        if (strcmp(o->ix.it[k].item, iname) == 0) found = 1;
    if (!found) return 0;               /* not a registered index */

    char kb[40];
    const char *kp;
    int64_t kl = mv_val_chars(key, kb, sizeof kb, &kp);
    mvx_cursor *c = b->driver->index_select(f, iname, kp, kl);
    if (!c) return 0;

    store_state *st = state(ctx);
    clear_select(st);
    st->sel_active = 1;
    int64_t cap = 0;
    mv_value rid;
    mv_init(&rid);
    while (b->driver->select_next(c, &rid)) {
        char rb[40];
        const char *rp;
        int64_t rl = mv_val_chars(&rid, rb, sizeof rb, &rp);
        sel_push(st, &cap, rp, rl);
    }
    mv_clear(&rid);
    b->driver->select_end(c);
    return 1;
}

void mvx_release(mvx_ctx *ctx, const mv_value *fvar, const mv_value *id) {
    store_state *st = state(ctx);
    if (!fvar) {                        /* bare RELEASE: drop everything */
        for (lock_ent *l = st->locks; l;) {
            lock_ent *n = l->next;
            if (l->f) {
                mvx_file_base *lb = (mvx_file_base *)l->f;
                if (lb->driver->unlock) {
                    const char *id = strchr(l->key, '\x01') + 1;
                    lb->driver->unlock(l->f, id, (int64_t)strlen(id));
                }
            }
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
    st->sel_active = 1;

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

void mvx_formlist(mvx_ctx *ctx, const mv_value *ids) {
    store_state *st = state(ctx);
    clear_select(st);
    st->sel_active = 1;
    char nb[40];
    const char *p;
    int64_t len = mv_val_chars(ids, nb, sizeof nb, &p);
    int64_t cap = 0;
    const char *end = p + len;
    while (p < end) {
        const char *am = memchr(p, '\xFE', (size_t)(end - p));
        int64_t n = (am ? am : end) - p;
        if (n > 0) sel_push(st, &cap, p, n);
        p = am ? am + 1 : end;
    }
}

/* SYSTEM(11): is a select list active (in-process or session)? */
int64_t mvx_list_active(mvx_ctx *ctx) {
    store_state *st = state(ctx);
    if (st->sel_active) return st->sel_pos < st->sel_n;
    const char *sf = getenv("MVXSESSION");
    if (!sf || !sf[0]) return 0;
    FILE *fp = fopen(sf, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fclose(fp);
    return sz > 0;
}

int64_t mvx_readnext(mvx_ctx *ctx, mv_value *id) {
    store_state *st = state(ctx);
    session_load(st);
    if (st->sel_pos >= st->sel_n) return 0;
    mv_copy(id, &st->sel_ids[st->sel_pos++]);
    return 1;
}

/* FILELIST(): every MV file in the account — subdirectories (directory
   driver) plus LMDB named DBs, as "name @VM type" attributes.  DICT
   stores and infrastructure directories are filtered out. */
void mvx_filelist(mvx_ctx *ctx, mv_value *dst) {
    (void)ctx;
    char *buf = NULL;
    size_t len = 0, cap = 0;

#define FL_PUT(p, n, ty)                                                  \
    do {                                                                  \
        size_t need = (n) + 3;                                            \
        if (len + need > cap) {                                           \
            cap = cap ? cap * 2 : 256;                                    \
            while (cap < len + need) cap *= 2;                            \
            char *nb = realloc(buf, cap);                                 \
            if (!nb) mvx_fatal("out of memory in FILELIST");              \
            buf = nb;                                                     \
        }                                                                 \
        if (len) buf[len++] = (char)0xFE;                                 \
        memcpy(buf + len, (p), (n));                                      \
        len += (n);                                                       \
        buf[len++] = (char)0xFD;                                          \
        buf[len++] = (ty);                                                \
    } while (0)

    const char *acct = getenv("MVXACCOUNT");
    if (!acct || !acct[0]) acct = ".";
    /* scandir + alphasort, not readdir: directory order is filesystem-
       dependent (APFS returns sorted, ext4 hash order), so LISTF would
       otherwise vary by platform.  A file listing should be stable. */
    struct dirent **ents = NULL;
    int ne = scandir(acct, &ents, NULL, alphasort);
    for (int i = 0; i < ne; i++) {
        const char *nm = ents[i]->d_name;
        if (nm[0] != '.' && strcmp(nm, "mvxdata.lmdb") != 0) {
            char p[4096];
            snprintf(p, sizeof p, "%s/%s", acct, nm);
            struct stat sb;
            if (stat(p, &sb) == 0 && S_ISDIR(sb.st_mode))
                FL_PUT(nm, strlen(nm), 'D');
        }
        free(ents[i]);
    }
    free(ents);

    const mvx_driver *lmdb = driver_load("lmdb");
    if (lmdb->names) {
        mv_value names;
        mv_init(&names);
        char err[256] = "";
        if (lmdb->names(&names, err, sizeof err) &&
            names.tag == MV_STR && names.s->len > 0) {
            const char *p = names.s->data, *end = p + names.s->len;
            while (p < end) {
                const char *am = memchr(p, '\xFE', (size_t)(end - p));
                size_t n = (am ? am : end) - p;
                int internal = (n > 5 && memcmp(p, "DICT.", 5) == 0) ||
                               memmem(p, n, ".IDX.", 5) != NULL;
                if (n > 0 && !internal) FL_PUT(p, n, 'L');
                p = am ? am + 1 : end;
            }
        }
        mv_clear(&names);
    }
    /* Bound files: the BINDINGS record names them (exactly).  Their
       type is the driver name, so LISTF distinguishes lmdbnet from
       postgres and the like.  A "*" entry is a policy, not a file. */
    int listed_bound = 0;
    {
        const char *acct2 = getenv("MVXACCOUNT");
        if (!acct2 || !acct2[0]) acct2 = ".";
        char rpath[4096];
        snprintf(rpath, sizeof rpath, "%s/BINDINGS", acct2);
        FILE *rf = fopen(rpath, "r");
        if (rf) {
            listed_bound = 1;
            char ln[1152];
            while (fgets(ln, sizeof ln, rf)) {
                char *p = ln;
                while (*p == ' ' || *p == '\t') p++;
                char *sp2 = p;
                while (*sp2 && *sp2 != ' ' && *sp2 != '\t' &&
                       *sp2 != '\n' && *sp2 != '\r')
                    sp2++;
                size_t n = (size_t)(sp2 - p);
                if (n == 0 || *p == '#' || (n == 1 && p[0] == '*'))
                    continue;
                char *dp = sp2;
                while (*dp == ' ' || *dp == '\t') dp++;
                char *de = dp;
                while (*de && *de != ' ' && *de != '\t' &&
                       *de != '\n' && *de != '\r')
                    de++;
                /* emit "name @VM driver" with an explicit driver type */
                if (len) buf[len++] = (char)0xFE;
                if (len + n > cap) { cap = cap ? cap*2 : 256;
                    while (cap < len + n) cap *= 2;
                    char *nb = realloc(buf, cap);
                    if (!nb) mvx_fatal("out of memory in FILELIST");
                    buf = nb; }
                memcpy(buf + len, p, n); len += n;
                if (len + 1 > cap) { cap *= 2;
                    char *nb = realloc(buf, cap);
                    if (!nb) mvx_fatal("out of memory in FILELIST");
                    buf = nb; }
                buf[len++] = (char)0xFD;
                size_t dl = (size_t)(de - dp);
                if (len + dl > cap) { cap = cap ? cap*2 : 256;
                    while (cap < len + dl) cap *= 2;
                    char *nb = realloc(buf, cap);
                    if (!nb) mvx_fatal("out of memory in FILELIST");
                    buf = nb; }
                memcpy(buf + len, dp, dl); len += dl;
            }
            fclose(rf);
        }
    }
    const char *dmn = getenv("MVXDAEMON");
    if (!listed_bound && dmn && dmn[0]) {
        const mvx_driver *net = driver_load("lmdbnet");
        if (net->names) {
            mv_value names;
            mv_init(&names);
            char err[256] = "";
            if (net->names(&names, err, sizeof err) &&
                names.tag == MV_STR && names.s->len > 0) {
                const char *p = names.s->data;
                const char *end = p + names.s->len;
                while (p < end) {
                    const char *am = memchr(p, '\xFE',
                                            (size_t)(end - p));
                    size_t n = (am ? am : end) - p;
                    int internal =
                        (n > 5 && memcmp(p, "DICT.", 5) == 0) ||
                        memmem(p, n, ".IDX.", 5) != NULL;
                    if (n > 0 && !internal) FL_PUT(p, n, 'N');
                    p = am ? am + 1 : end;
                }
            }
            mv_clear(&names);
        }
    }
#undef FL_PUT

    mv_set_str(dst, buf ? buf : "", (int64_t)len);
    free(buf);
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

/* Maintain the BINDINGS record (CREATE-FILE writes it, DELETE-FILE
   cleans it): one "SPEC driver {params}" line per bound file. */
static void binding_add(const char *cspec, const char *driver,
                        const char *params) {
    const char *acct = getenv("MVXACCOUNT");
    if (!acct || !acct[0]) acct = ".";
    char path[4096];
    snprintf(path, sizeof path, "%s/BINDINGS", acct);
    FILE *rp = fopen(path, "r");    /* skip if an exact entry exists */
    if (rp) {
        char ln[1152];
        size_t cl = strlen(cspec);
        while (fgets(ln, sizeof ln, rp)) {
            char *p = ln;
            while (*p == ' ' || *p == '\t') p++;
            char *sp2 = p;
            while (*sp2 && *sp2 != ' ' && *sp2 != '\t' &&
                   *sp2 != '\n' && *sp2 != '\r')
                sp2++;
            if ((size_t)(sp2 - p) == cl && memcmp(p, cspec, cl) == 0) {
                fclose(rp);
                return;
            }
        }
        fclose(rp);
    }
    FILE *fp = fopen(path, "a");
    if (!fp) return;
    if (params && params[0])
        fprintf(fp, "%s %s %s\n", cspec, driver, params);
    else
        fprintf(fp, "%s %s\n", cspec, driver);
    fclose(fp);
}

static void binding_remove(const char *cspec) {
    const char *acct = getenv("MVXACCOUNT");
    if (!acct || !acct[0]) acct = ".";
    char path[4096];
    snprintf(path, sizeof path, "%s/BINDINGS", acct);
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char out[16384];
    size_t olen = 0;
    char ln[1152];
    size_t cl = strlen(cspec);
    int changed = 0;
    while (fgets(ln, sizeof ln, fp)) {
        char *p = ln;
        while (*p == ' ' || *p == '\t') p++;
        char *sp2 = p;
        while (*sp2 && *sp2 != ' ' && *sp2 != '\t' && *sp2 != '\n' &&
               *sp2 != '\r')
            sp2++;
        if ((size_t)(sp2 - p) == cl && memcmp(p, cspec, cl) == 0) {
            changed = 1;
            continue;
        }
        size_t n = strlen(ln);
        if (olen + n < sizeof out) {
            memcpy(out + olen, ln, n);
            olen += n;
        }
    }
    fclose(fp);
    if (!changed) return;
    if (olen == 0) {
        unlink(path);
        return;
    }
    fp = fopen(path, "w");
    if (!fp) return;
    fwrite(out, 1, olen, fp);
    fclose(fp);
}

/* Stamp a %FILE% control record into a just-created dictionary: a
   metadata hint that travels with the schema in git (BUILD reads it to
   know a file's backend after a clone).  Record = "FILE" VM type VM
   conn (attribute 1). */
static void write_file_meta(const mvx_driver *drv, const char *dictspec,
                            const char *type, const char *conn) {
    char err[256] = "";
    mvx_file *d = drv->open(dictspec, err, sizeof err);
    if (!d) return;
    char rec[700];
    size_t n = 0;
    memcpy(rec + n, "FILE", 4); n += 4;
    rec[n++] = (char)0xFD;              /* value mark */
    size_t tl = strlen(type);
    memcpy(rec + n, type, tl); n += tl;
    if (conn && conn[0]) {
        rec[n++] = (char)0xFD;
        size_t cl = strlen(conn);
        memcpy(rec + n, conn, cl); n += cl;
    }
    mv_value rv;
    mv_init(&rv);
    mv_set_str(&rv, rec, (int64_t)n);
    drv->write(d, "%FILE%", 6, &rv);
    mv_clear(&rv);
    drv->close(d);
}

int64_t mvx_createfile(mvx_ctx *ctx, const mv_value *spec,
                       const mv_value *type) {
    (void)ctx;
    char cspec[1024];
    if (!spec_cstr(spec, cspec, sizeof cspec)) return 0;

    char tb[600];
    const char *tp = "";
    if (type) mv_val_chars(type, tb, sizeof tb, &tp);
    char err[256] = "";

    /* CREATE-FILE name USING <driver> {params}: bind at creation and
       create through that driver.  The binding is recorded in BINDINGS
       so every later OPEN resolves there. */
    if (strncasecmp(tp, "USING ", 6) == 0 ||
        strcasecmp(tp, "USING") == 0) {
        const char *dp2 = tp + 5;
        while (*dp2 == ' ' || *dp2 == '\t') dp2++;
        char drvname[64];
        const char *de = dp2;
        while (*de && *de != ' ' && *de != '\t') de++;
        snprintf(drvname, sizeof drvname, "%.*s", (int)(de - dp2), dp2);
        const char *ap = de;
        while (*ap == ' ' || *ap == '\t') ap++;
        if (!drvname[0]) {
            fprintf(stderr, "CREATE-FILE USING: name a driver "
                            "(e.g. lmdbnet)\n");
            return 0;
        }
        const char *envd = getenv("MVXDAEMON");
        if (strcmp(drvname, "lmdbnet") == 0 && !ap[0] &&
            (!envd || !envd[0])) {
            fprintf(stderr, "CREATE-FILE USING lmdbnet: no daemon "
                            "address (give one, or set $MVXDAEMON)\n");
            return 0;
        }
        binding_add(cspec, drvname, ap);
        char dataspec[1720], dictspec[1720];
        const mvx_driver *drv =
            resolve(cspec, 0, dataspec, sizeof dataspec);
        if (!drv->create(dataspec, err, sizeof err)) {
            if (err[0]) fprintf(stderr, "CREATE-FILE: %s\n", err);
            binding_remove(cspec);
            return 0;
        }
        resolve(cspec, 1, dictspec, sizeof dictspec);
        if (!drv->create(dictspec, err, sizeof err)) {
            drv->remove(dataspec, err, sizeof err);
            binding_remove(cspec);
            return 0;
        }
        write_file_meta(drv, dictspec, drvname, ap);
        return 1;
    }

    if (tp[0] == 'D' || tp[0] == 'd') {
        const mvx_driver *drv = driver_load("dir");
        if (!drv->create(cspec, err, sizeof err)) return 0;
        /* A dictionary directory (NAME.DICT) is a file but needs no
           dictionary of its own — never create NAME.DICT.DICT. */
        size_t cl = strlen(cspec);
        if (cl > 5 && strcmp(cspec + cl - 5, ".DICT") == 0) return 1;
        char dspec[1152];
        snprintf(dspec, sizeof dspec, "%s.DICT", cspec);
        if (!drv->create(dspec, err, sizeof err)) {
            drv->remove(cspec, err, sizeof err);
            return 0;
        }
        write_file_meta(drv, dspec, "dir", "");
        return 1;
    }

    /* LMDB-backed: honour the file's local/remote binding.  DICT and
       DATA are created together, classic style. */
    char dataspec[1720], dictspec[1720];
    const mvx_driver *drv = resolve(cspec, 0, dataspec, sizeof dataspec);
    if (!drv->create(dataspec, err, sizeof err)) return 0;
    resolve(cspec, 1, dictspec, sizeof dictspec);
    if (!drv->create(dictspec, err, sizeof err)) {
        drv->remove(dataspec, err, sizeof err);
        return 0;
    }
    write_file_meta(drv, dictspec, "lmdb", "");
    return 1;
}

int64_t mvx_deletefile(mvx_ctx *ctx, const mv_value *spec) {
    (void)ctx;
    char cspec[1024];
    if (!spec_cstr(spec, cspec, sizeof cspec)) return 0;

    char dspec[1720], rspec[1720];
    const mvx_driver *drv = resolve(cspec, 1, dspec, sizeof dspec);
    char err[256] = "";
    drv->remove(dspec, err, sizeof err);        /* dict first, may be absent */
    resolve(cspec, 0, rspec, sizeof rspec);
    int64_t r = drv->remove(rspec, err, sizeof err);
    if (r) binding_remove(cspec);           /* binding dies with it */
    return r;
}
