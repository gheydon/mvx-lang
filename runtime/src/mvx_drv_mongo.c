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

/* mongo driver — a MultiValue file on a MongoDB collection.
 *
 * An MV record is a natural document, so each record is stored as
 * { _id: <id>, rec: <blob> } with both fields BinData, so ids and records
 * round-trip byte-exact (marks and all).  The account/namespace maps to a
 * database and each file to a collection.  The connection is a named profile
 * (BINDINGS `ORDERS @mongomain`, .mvx-private/connections carries
 * driver/address/namespace/user/password) — the same indirection the postgres
 * and lmdbnet drivers use, or a raw mongodb:// URI.
 *
 * Minimal contract only for this first cut: no relational mapping, query
 * push-down, native indexes, or lock authority — the runtime falls back to a
 * client-side scan, per-record projection, and its process-local lock table.
 * Those are follow-ups (#61).
 */
#include "../include/mvx_driver.h"

#include <mongoc/mongoc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONNS 8
static struct {
    char loc[1024];
    mongoc_client_t *client;
} g_conns[MAX_CONNS];
static int g_nconns;

struct mvx_cursor {
    mv_value *ids;
    int64_t n, pos, cap;
};

typedef struct {
    mvx_file_base base;
    mongoc_client_t *client;
    char db[128];                       /* namespace -> database */
    char coll[256];                     /* file spec -> collection */
} mongo_file;

static const mvx_driver mvx_driver_mongo;

static void ensure_init(void) {
    static int done;
    if (!done) { mongoc_init(); done = 1; }
}

/* Split "loc\nspec" into loc (connection) and the bare file spec. */
static const char *split_spec(const char *spec, char *loc, size_t cap) {
    const char *nl = strchr(spec, '\n');
    if (!nl) { loc[0] = '\0'; return spec; }
    size_t n = (size_t)(nl - spec);
    if (n >= cap) n = cap - 1;
    memcpy(loc, spec, n);
    loc[n] = '\0';
    return nl + 1;
}

/* Connect (once per location).  loc is "@connname" (resolved from the
   connection profile) or a raw mongodb:// URI; the namespace maps to a
   database, filled into `db`. */
static mongoc_client_t *mongo_connect(const char *loc, char *db, size_t dbcap,
                                      char *err, size_t errlen) {
    ensure_init();

    /* resolve the database (namespace) regardless of connection cache */
    if (loc[0] == '@') {
        const char *cn = loc + 1;
        if (!mvx_conn_lookup(cn, "namespace", db, dbcap))
            mvx_account_namespace(db, dbcap);
    } else {
        mvx_account_namespace(db, dbcap);
    }
    if (!db[0]) snprintf(db, dbcap, "mvx");

    for (int i = 0; i < g_nconns; i++)
        if (strcmp(g_conns[i].loc, loc) == 0) return g_conns[i].client;

    if (g_nconns >= MAX_CONNS) {
        snprintf(err, errlen, "mongo: too many connections");
        return NULL;
    }

    char uri[1200];
    if (loc[0] == '@') {
        const char *cn = loc + 1;
        char address[256] = "", user[128] = "", password[256] = "";
        mvx_conn_lookup(cn, "address", address, sizeof address);
        mvx_conn_lookup(cn, "user", user, sizeof user);
        mvx_conn_lookup(cn, "password", password, sizeof password);
        if (!address[0]) snprintf(address, sizeof address, "localhost:27017");
        if (user[0])
            snprintf(uri, sizeof uri, "mongodb://%s:%s@%s", user, password,
                     address);
        else
            snprintf(uri, sizeof uri, "mongodb://%s", address);
    } else {
        snprintf(uri, sizeof uri, "%s", loc);   /* raw mongodb:// URI */
    }

    /* mongoc_client_new parses the URI itself and exists across driver v1 and
       v2, so one code path builds against Homebrew's libmongoc 2.x and apt's
       1.x alike; it returns NULL only on an unparseable URI. */
    mongoc_client_t *c = mongoc_client_new(uri);
    if (!c) {
        snprintf(err, errlen, "mongo: invalid connection URI '%s'", uri);
        return NULL;
    }
    snprintf(g_conns[g_nconns].loc, sizeof g_conns[0].loc, "%s", loc);
    g_conns[g_nconns].client = c;
    g_nconns++;
    return c;
}

/* A collection handle for one operation (cheap; destroy after). */
static mongoc_collection_t *coll_of(mongo_file *f) {
    return mongoc_client_get_collection(f->client, f->db, f->coll);
}

/* Selector { _id: <id> } as BinData. */
static void sel_id(bson_t *sel, const char *id, int64_t idlen) {
    bson_init(sel);
    bson_append_binary(sel, "_id", 3, BSON_SUBTYPE_BINARY,
                       (const uint8_t *)id, (uint32_t)idlen);
}

static mvx_file *mongo_open(const char *spec, char *err, size_t errlen) {
    char loc[1024];
    const char *rspec = split_spec(spec, loc, sizeof loc);
    char db[128];
    mongoc_client_t *c = mongo_connect(loc, db, sizeof db, err, errlen);
    if (!c) return NULL;
    mongoc_database_t *d = mongoc_client_get_database(c, db);
    bson_error_t berr;
    bool exists = mongoc_database_has_collection(d, rspec, &berr);
    mongoc_database_destroy(d);
    if (!exists) return NULL;             /* not found: normal ELSE path */

    mongo_file *f = calloc(1, sizeof(mongo_file));
    if (!f) mvx_fatal("out of memory opening %s", spec);
    f->base.driver = &mvx_driver_mongo;
    f->base.spec = strdup(spec);
    f->client = c;
    snprintf(f->db, sizeof f->db, "%s", db);
    snprintf(f->coll, sizeof f->coll, "%s", rspec);
    return (mvx_file *)f;
}

static void mongo_close(mvx_file *fh) {
    mongo_file *f = (mongo_file *)fh;
    free(f->base.spec);
    free(f);                              /* the client is pooled */
}

static int mongo_create(const char *spec, char *err, size_t errlen) {
    char loc[1024];
    const char *rspec = split_spec(spec, loc, sizeof loc);
    char db[128];
    mongoc_client_t *c = mongo_connect(loc, db, sizeof db, err, errlen);
    if (!c) return 0;
    mongoc_database_t *d = mongoc_client_get_database(c, db);
    bson_error_t berr;
    if (mongoc_database_has_collection(d, rspec, &berr)) {
        mongoc_database_destroy(d);
        return 0;                         /* already exists (not an error) */
    }
    mongoc_collection_t *coll =
        mongoc_database_create_collection(d, rspec, NULL, &berr);
    mongoc_database_destroy(d);
    if (!coll) {
        snprintf(err, errlen, "mongo: %s", berr.message);
        return 0;
    }
    mongoc_collection_destroy(coll);
    return 1;
}

static int mongo_remove(const char *spec, char *err, size_t errlen) {
    char loc[1024];
    const char *rspec = split_spec(spec, loc, sizeof loc);
    char db[128];
    mongoc_client_t *c = mongo_connect(loc, db, sizeof db, err, errlen);
    if (!c) return 0;
    mongoc_database_t *d = mongoc_client_get_database(c, db);
    bson_error_t berr;
    int exists = mongoc_database_has_collection(d, rspec, &berr);
    mongoc_database_destroy(d);
    if (!exists) return 0;                /* nothing to drop */
    mongoc_collection_t *coll = mongoc_client_get_collection(c, db, rspec);
    bool ok = mongoc_collection_drop(coll, &berr);
    mongoc_collection_destroy(coll);
    if (!ok) snprintf(err, errlen, "mongo: %s", berr.message);
    return ok ? 1 : 0;
}

static int mongo_read(mvx_file *fh, const char *id, int64_t idlen,
                      mv_value *rec) {
    mongo_file *f = (mongo_file *)fh;
    mongoc_collection_t *coll = coll_of(f);
    bson_t filter, opts;
    sel_id(&filter, id, idlen);
    bson_init(&opts);
    bson_append_int64(&opts, "limit", 5, 1);
    mongoc_cursor_t *cur =
        mongoc_collection_find_with_opts(coll, &filter, &opts, NULL);
    const bson_t *doc;
    int found = 0;
    if (mongoc_cursor_next(cur, &doc)) {
        bson_iter_t it;
        if (bson_iter_init_find(&it, doc, "rec") && BSON_ITER_HOLDS_BINARY(&it)) {
            bson_subtype_t st;
            uint32_t len;
            const uint8_t *data;
            bson_iter_binary(&it, &st, &len, &data);
            mv_set_str(rec, (const char *)data, (int64_t)len);
            found = 1;
        }
    }
    mongoc_cursor_destroy(cur);
    bson_destroy(&filter);
    bson_destroy(&opts);
    mongoc_collection_destroy(coll);
    return found;
}

static int mongo_write(mvx_file *fh, const char *id, int64_t idlen,
                       const mv_value *rec) {
    mongo_file *f = (mongo_file *)fh;
    mongoc_collection_t *coll = coll_of(f);
    char nb[40];
    const char *rp;
    int64_t rl = mv_val_chars(rec, nb, sizeof nb, &rp);
    bson_t sel, doc, opts;
    sel_id(&sel, id, idlen);
    bson_init(&doc);
    bson_append_binary(&doc, "_id", 3, BSON_SUBTYPE_BINARY,
                       (const uint8_t *)id, (uint32_t)idlen);
    bson_append_binary(&doc, "rec", 3, BSON_SUBTYPE_BINARY,
                       (const uint8_t *)rp, (uint32_t)rl);
    bson_init(&opts);
    bson_append_bool(&opts, "upsert", 6, true);
    bson_error_t berr;
    bool ok = mongoc_collection_replace_one(coll, &sel, &doc, &opts, NULL,
                                            &berr);
    bson_destroy(&sel);
    bson_destroy(&doc);
    bson_destroy(&opts);
    mongoc_collection_destroy(coll);
    return ok ? 1 : 0;
}

static int mongo_del(mvx_file *fh, const char *id, int64_t idlen) {
    mongo_file *f = (mongo_file *)fh;
    mongoc_collection_t *coll = coll_of(f);
    bson_t sel, reply;
    sel_id(&sel, id, idlen);
    bson_error_t berr;
    int deleted = 0;
    if (mongoc_collection_delete_one(coll, &sel, NULL, &reply, &berr)) {
        bson_iter_t it;
        if (bson_iter_init_find(&it, &reply, "deletedCount"))
            deleted = bson_iter_as_int64(&it) > 0;
        bson_destroy(&reply);
    }
    bson_destroy(&sel);
    mongoc_collection_destroy(coll);
    return deleted;
}

/* Snapshot the id list up front (a short read), then stream it. */
static mvx_cursor *mongo_select_begin(mvx_file *fh) {
    mongo_file *f = (mongo_file *)fh;
    mvx_cursor *c = calloc(1, sizeof(mvx_cursor));
    if (!c) mvx_fatal("out of memory in mongo select");
    mongoc_collection_t *coll = coll_of(f);
    bson_t filter, opts, proj;
    bson_init(&filter);                   /* {} — every document */
    bson_init(&proj);
    bson_append_int32(&proj, "_id", 3, 1);
    bson_init(&opts);
    bson_append_document(&opts, "projection", 10, &proj);
    mongoc_cursor_t *mc =
        mongoc_collection_find_with_opts(coll, &filter, &opts, NULL);
    const bson_t *doc;
    while (mongoc_cursor_next(mc, &doc)) {
        bson_iter_t it;
        if (!bson_iter_init_find(&it, doc, "_id") || !BSON_ITER_HOLDS_BINARY(&it))
            continue;
        bson_subtype_t st;
        uint32_t len;
        const uint8_t *data;
        bson_iter_binary(&it, &st, &len, &data);
        if (c->n == c->cap) {
            c->cap = c->cap ? c->cap * 2 : 64;
            c->ids = realloc(c->ids, (size_t)c->cap * sizeof(mv_value));
            if (!c->ids) mvx_fatal("out of memory in mongo select");
        }
        mv_init(&c->ids[c->n]);
        mv_set_str(&c->ids[c->n], (const char *)data, (int64_t)len);
        c->n++;
    }
    mongoc_cursor_destroy(mc);
    bson_destroy(&filter);
    bson_destroy(&opts);
    bson_destroy(&proj);
    mongoc_collection_destroy(coll);
    return c;
}

static int mongo_select_next(mvx_cursor *c, mv_value *id) {
    if (!c || c->pos >= c->n) return 0;
    mv_copy(id, &c->ids[c->pos++]);
    return 1;
}

static void mongo_select_end(mvx_cursor *c) {
    if (!c) return;
    for (int64_t i = 0; i < c->n; i++) mv_clear(&c->ids[i]);
    free(c->ids);
    free(c);
}

static int64_t mongo_select_count(mvx_cursor *c) { return c ? c->n : 0; }

/* LISTF: the collections in this account's database, @AM-separated. */
static int mongo_names(const char *loc, mv_value *out, char *err,
                       size_t errlen) {
    char db[128];
    mongoc_client_t *c = mongo_connect(loc, db, sizeof db, err, errlen);
    if (!c) return 0;
    mongoc_database_t *d = mongoc_client_get_database(c, db);
    bson_error_t berr;
    char **names = mongoc_database_get_collection_names_with_opts(d, NULL, &berr);
    mongoc_database_destroy(d);
    if (!names) {
        snprintf(err, errlen, "mongo: %s", berr.message);
        return 0;
    }
    char buf[8192];
    size_t p = 0;
    for (int i = 0; names[i]; i++) {
        int n = snprintf(buf + p, sizeof buf - p, "%s%s", p ? "\xFE" : "",
                         names[i]);
        if (n < 0 || (size_t)n >= sizeof buf - p) break;
        p += (size_t)n;
    }
    bson_strfreev(names);
    mv_set_str(out, buf, (int64_t)p);
    return 1;
}

static const mvx_driver mvx_driver_mongo = {
    .name = "mongo",
    .open = mongo_open,
    .close = mongo_close,
    .read = mongo_read,
    .write = mongo_write,
    .del = mongo_del,
    .select_begin = mongo_select_begin,
    .select_next = mongo_select_next,
    .select_end = mongo_select_end,
    .create = mongo_create,
    .remove = mongo_remove,
    .names = mongo_names,
    .select_count = mongo_select_count,
    /* All other capability hooks are NULL: no relational mapping, query
       push-down, native index, lock authority, or bulk batching in this first
       cut — the runtime falls back to a client-side scan, per-record
       projection, and its process-local lock table.  Follow-ups (#61). */
};

const mvx_driver *mvx_driver_entry(int abi) {
    return abi == MVX_DRIVER_ABI ? &mvx_driver_mongo : NULL;
}
