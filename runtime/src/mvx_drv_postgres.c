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

/* postgres driver — a MultiValue file on a PostgreSQL table.
 *
 * Each account/namespace is a schema; each file is a table
 * (id BYTEA PRIMARY KEY, rec BYTEA) in it, so records round-trip
 * byte-exact (marks and all).  The connection is a named profile
 * (BINDINGS `ORDERS @pgmain`, .mvx-private/connections carries
 * driver/address/dbname/user/password/namespace) — the same indirection
 * the lmdbnet driver uses.
 *
 * Minimal contract only: no native secondary indexes (the runtime falls
 * back to a scan) and no lock authority (the process-local lock table
 * applies) in this first cut — both are follow-ups.
 */
#include "../include/mvx_driver.h"

#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONNS 8
static struct {
    char loc[1024];
    PGconn *conn;
} g_conns[MAX_CONNS];
static int g_nconns;

struct mvx_cursor {
    mv_value *ids;
    int64_t n, pos;
};

typedef struct {
    mvx_file_base base;
    PGconn *conn;
    char schema[128];
    char table[256];
} pg_file;

static const mvx_driver mvx_driver_postgres;

/* Keep libpq NOTICEs (e.g. "schema already exists") off the client. */
static void noop_notice(void *arg, const char *message) {
    (void)arg;
    (void)message;
}

/* Connect (once per location).  loc is "@connname" (resolved from the
   connection profile) or a raw libpq conninfo string; the namespace maps
   to a schema. */
static PGconn *pg_connect(const char *loc, char *schema, size_t scap,
                          char *err, size_t errlen) {
    for (int i = 0; i < g_nconns; i++)
        if (strcmp(g_conns[i].loc, loc) == 0) {
            /* schema still needed by the caller */
            break;
        }

    /* resolve the schema regardless of cache hit */
    if (loc[0] == '@') {
        const char *cn = loc + 1;
        if (!mvx_conn_lookup(cn, "namespace", schema, scap))
            mvx_account_namespace(schema, scap);
    } else {
        mvx_account_namespace(schema, scap);
    }

    for (int i = 0; i < g_nconns; i++)
        if (strcmp(g_conns[i].loc, loc) == 0) return g_conns[i].conn;

    if (g_nconns >= MAX_CONNS) {
        snprintf(err, errlen, "postgres: too many connections");
        return NULL;
    }

    PGconn *c;
    if (loc[0] == '@') {
        const char *cn = loc + 1;
        char address[256] = "", dbname[128] = "", user[128] = "",
             password[256] = "";
        mvx_conn_lookup(cn, "address", address, sizeof address);
        mvx_conn_lookup(cn, "dbname", dbname, sizeof dbname);
        mvx_conn_lookup(cn, "user", user, sizeof user);
        mvx_conn_lookup(cn, "password", password, sizeof password);
        char host[256] = "", port[16] = "5432";
        const char *colon = strrchr(address, ':');
        if (colon) {
            size_t hl = (size_t)(colon - address);
            if (hl >= sizeof host) hl = sizeof host - 1;
            memcpy(host, address, hl);
            host[hl] = '\0';
            snprintf(port, sizeof port, "%s", colon + 1);
        } else {
            snprintf(host, sizeof host, "%s", address);
        }
        const char *keys[] = {"host", "port",     "dbname",
                              "user", "password", NULL};
        const char *vals[] = {host, port, dbname, user, password, NULL};
        c = PQconnectdbParams(keys, vals, 0);
    } else {
        c = PQconnectdb(loc);
    }
    if (!c || PQstatus(c) != CONNECTION_OK) {
        snprintf(err, errlen, "postgres: %s",
                 c ? PQerrorMessage(c) : "connection failed");
        if (c) PQfinish(c);
        return NULL;
    }
    PQsetNoticeProcessor(c, noop_notice, NULL);   /* swallow NOTICEs */
    snprintf(g_conns[g_nconns].loc, sizeof g_conns[0].loc, "%s", loc);
    g_conns[g_nconns].conn = c;
    g_nconns++;
    return c;
}

/* "schema"."table", identifiers safely quoted. */
static void qualify(PGconn *c, const char *schema, const char *table,
                    char *out, size_t cap) {
    char *s = PQescapeIdentifier(c, schema, strlen(schema));
    char *t = PQescapeIdentifier(c, table, strlen(table));
    snprintf(out, cap, "%s.%s", s ? s : "\"\"", t ? t : "\"\"");
    if (s) PQfreemem(s);
    if (t) PQfreemem(t);
}

/* Split "loc\nspec" into loc and the bare file spec. */
static const char *split_spec(const char *spec, char *loc, size_t cap) {
    const char *nl = strchr(spec, '\n');
    if (!nl) {
        loc[0] = '\0';
        return spec;
    }
    size_t n = (size_t)(nl - spec);
    if (n >= cap) n = cap - 1;
    memcpy(loc, spec, n);
    loc[n] = '\0';
    return nl + 1;
}

static mvx_file *pg_open(const char *spec, char *err, size_t errlen) {
    char loc[1024];
    const char *rspec = split_spec(spec, loc, sizeof loc);
    char schema[128];
    PGconn *c = pg_connect(loc, schema, sizeof schema, err, errlen);
    if (!c) return NULL;
    char qt[512];
    qualify(c, schema, rspec, qt, sizeof qt);
    const char *pv[1] = {qt};             /* "schema"."table" as text */
    PGresult *r = PQexecParams(c, "SELECT to_regclass($1)", 1, NULL, pv,
                               NULL, NULL, 0);
    int exists = r && PQresultStatus(r) == PGRES_TUPLES_OK &&
                 PQntuples(r) == 1 && !PQgetisnull(r, 0, 0);
    if (r) PQclear(r);
    if (!exists) return NULL;             /* not found: normal ELSE path */

    pg_file *f = calloc(1, sizeof(pg_file));
    if (!f) mvx_fatal("out of memory opening %s", spec);
    f->base.driver = &mvx_driver_postgres;
    f->base.spec = strdup(spec);
    f->conn = c;
    snprintf(f->schema, sizeof f->schema, "%s", schema);
    snprintf(f->table, sizeof f->table, "%s", rspec);
    return (mvx_file *)f;
}

static void pg_close(mvx_file *fh) {
    pg_file *f = (pg_file *)fh;
    free(f->base.spec);
    free(f);                              /* the PGconn is pooled */
}

static int pg_read(mvx_file *fh, const char *id, int64_t idlen,
                   mv_value *rec) {
    pg_file *f = (pg_file *)fh;
    char qt[512];
    qualify(f->conn, f->schema, f->table, qt, sizeof qt);
    char sql[640];
    snprintf(sql, sizeof sql, "SELECT rec FROM %s WHERE id=$1", qt);
    const char *pv[1] = {id};
    int pl[1] = {(int)idlen};
    int pf[1] = {1};                      /* binary id */
    PGresult *r = PQexecParams(f->conn, sql, 1, NULL, pv, pl, pf, 1);
    int ok = r && PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) == 1;
    if (ok)
        mv_set_str(rec, PQgetvalue(r, 0, 0), PQgetlength(r, 0, 0));
    if (r) PQclear(r);
    return ok;
}

static int pg_write(mvx_file *fh, const char *id, int64_t idlen,
                    const mv_value *rec) {
    pg_file *f = (pg_file *)fh;
    char qt[512];
    qualify(f->conn, f->schema, f->table, qt, sizeof qt);
    char sql[768];
    snprintf(sql, sizeof sql,
             "INSERT INTO %s (id, rec) VALUES ($1,$2) "
             "ON CONFLICT (id) DO UPDATE SET rec=EXCLUDED.rec",
             qt);
    char nb[40];
    const char *rp;
    int64_t rl = mv_val_chars(rec, nb, sizeof nb, &rp);
    const char *pv[2] = {id, rp};
    int pl[2] = {(int)idlen, (int)rl};
    int pf[2] = {1, 1};                   /* binary id + rec */
    PGresult *r = PQexecParams(f->conn, sql, 2, NULL, pv, pl, pf, 0);
    int ok = r && PQresultStatus(r) == PGRES_COMMAND_OK;
    if (r) PQclear(r);
    return ok;
}

static int pg_del(mvx_file *fh, const char *id, int64_t idlen) {
    pg_file *f = (pg_file *)fh;
    char qt[512];
    qualify(f->conn, f->schema, f->table, qt, sizeof qt);
    char sql[640];
    snprintf(sql, sizeof sql, "DELETE FROM %s WHERE id=$1", qt);
    const char *pv[1] = {id};
    int pl[1] = {(int)idlen};
    int pf[1] = {1};
    PGresult *r = PQexecParams(f->conn, sql, 1, NULL, pv, pl, pf, 0);
    int deleted = r && PQresultStatus(r) == PGRES_COMMAND_OK &&
                  atoi(PQcmdTuples(r)) > 0;
    if (r) PQclear(r);
    return deleted;
}

/* Snapshot the id list up front (a short read), then stream it. */
static mvx_cursor *pg_select_begin(mvx_file *fh) {
    pg_file *f = (pg_file *)fh;
    mvx_cursor *cur = calloc(1, sizeof(mvx_cursor));
    if (!cur) mvx_fatal("out of memory in postgres select");
    char qt[512];
    qualify(f->conn, f->schema, f->table, qt, sizeof qt);
    char sql[640];
    snprintf(sql, sizeof sql, "SELECT id FROM %s", qt);
    PGresult *r = PQexecParams(f->conn, sql, 0, NULL, NULL, NULL, NULL, 1);
    if (r && PQresultStatus(r) == PGRES_TUPLES_OK) {
        int n = PQntuples(r);
        cur->ids = calloc(n ? n : 1, sizeof(mv_value));
        if (!cur->ids) mvx_fatal("out of memory in postgres select");
        for (int i = 0; i < n; i++) {
            mv_init(&cur->ids[cur->n]);
            mv_set_str(&cur->ids[cur->n], PQgetvalue(r, i, 0),
                       PQgetlength(r, i, 0));
            cur->n++;
        }
    }
    if (r) PQclear(r);
    return cur;
}

static int pg_select_next(mvx_cursor *c, mv_value *id) {
    if (!c || c->pos >= c->n) return 0;
    mv_copy(id, &c->ids[c->pos++]);
    return 1;
}

static void pg_select_end(mvx_cursor *c) {
    if (!c) return;
    for (int64_t i = 0; i < c->n; i++) mv_clear(&c->ids[i]);
    free(c->ids);
    free(c);
}

static int pg_create(const char *spec, char *err, size_t errlen) {
    char loc[1024];
    const char *rspec = split_spec(spec, loc, sizeof loc);
    char schema[128];
    PGconn *c = pg_connect(loc, schema, sizeof schema, err, errlen);
    if (!c) return 0;
    char *qs = PQescapeIdentifier(c, schema, strlen(schema));
    char csql[256];
    snprintf(csql, sizeof csql, "CREATE SCHEMA IF NOT EXISTS %s",
             qs ? qs : "\"\"");
    if (qs) PQfreemem(qs);
    PGresult *r = PQexec(c, csql);
    if (r) PQclear(r);

    char qt[512];
    qualify(c, schema, rspec, qt, sizeof qt);
    char sql[700];
    snprintf(sql, sizeof sql,
             "CREATE TABLE %s (id bytea primary key, rec bytea)", qt);
    r = PQexec(c, sql);
    int ok = r && PQresultStatus(r) == PGRES_COMMAND_OK;
    if (!ok && r) {
        /* 42P07 = duplicate_table: create returns 0 if it exists */
        const char *sqlstate = PQresultErrorField(r, PG_DIAG_SQLSTATE);
        if (!(sqlstate && strcmp(sqlstate, "42P07") == 0))
            snprintf(err, errlen, "postgres: %s", PQerrorMessage(c));
    }
    if (r) PQclear(r);
    return ok;
}

static int pg_remove(const char *spec, char *err, size_t errlen) {
    char loc[1024];
    const char *rspec = split_spec(spec, loc, sizeof loc);
    char schema[128];
    PGconn *c = pg_connect(loc, schema, sizeof schema, err, errlen);
    if (!c) return 0;
    char qt[512];
    qualify(c, schema, rspec, qt, sizeof qt);
    char sql[560];
    snprintf(sql, sizeof sql, "DROP TABLE %s", qt);
    PGresult *r = PQexec(c, sql);
    int ok = r && PQresultStatus(r) == PGRES_COMMAND_OK;
    if (r) PQclear(r);
    return ok;
}

/* Relational mapping (#18): project single-valued attributes into columns
   on the record's own table.  Phase-2 columns are text holding the
   display value; typed columns and child tables follow. */
static int pg_map_ensure(mvx_file *fh, const mvx_mapfield *cols, int ncols,
                         char *err, size_t errlen) {
    pg_file *f = (pg_file *)fh;
    char qt[512];
    qualify(f->conn, f->schema, f->table, qt, sizeof qt);
    for (int i = 0; i < ncols; i++) {
        char *qc = PQescapeIdentifier(f->conn, cols[i].name,
                                      strlen(cols[i].name));
        char sql[768];
        snprintf(sql, sizeof sql,
                 "ALTER TABLE %s ADD COLUMN IF NOT EXISTS %s text", qt,
                 qc ? qc : "\"\"");
        if (qc) PQfreemem(qc);
        PGresult *r = PQexec(f->conn, sql);
        int ok = r && PQresultStatus(r) == PGRES_COMMAND_OK;
        if (r) PQclear(r);
        if (!ok) {
            snprintf(err, errlen, "postgres: %s", PQerrorMessage(f->conn));
            return 0;
        }
    }
    return 1;
}

static int pg_map_apply(mvx_file *fh, const char *id, int64_t idlen,
                        const mvx_mapfield *cols, const char **vals,
                        const int64_t *vlens, int ncols) {
    pg_file *f = (pg_file *)fh;
    if (ncols <= 0) return 1;
    char qt[512];
    qualify(f->conn, f->schema, f->table, qt, sizeof qt);
    char sql[4096];
    size_t p = 0;
    p += (size_t)snprintf(sql + p, sizeof sql - p, "UPDATE %s SET ", qt);
    for (int i = 0; i < ncols && p < sizeof sql; i++) {
        char *qc = PQescapeIdentifier(f->conn, cols[i].name,
                                      strlen(cols[i].name));
        p += (size_t)snprintf(sql + p, sizeof sql - p, "%s%s=$%d",
                              i ? ", " : "", qc ? qc : "\"\"", i + 1);
        if (qc) PQfreemem(qc);
    }
    snprintf(sql + p, sizeof sql - p, " WHERE id=$%d", ncols + 1);

    const char *pv[64];
    int pl[64], pf[64];
    if (ncols > 63) return 0;
    for (int i = 0; i < ncols; i++) {
        pv[i] = vals[i];
        pl[i] = (int)vlens[i];
        pf[i] = 0;                        /* text */
    }
    pv[ncols] = id;
    pl[ncols] = (int)idlen;
    pf[ncols] = 1;                        /* binary id */
    PGresult *r = PQexecParams(f->conn, sql, ncols + 1, NULL, pv, pl, pf, 0);
    int ok = r && PQresultStatus(r) == PGRES_COMMAND_OK;
    if (r) PQclear(r);
    return ok;
}

static const mvx_driver mvx_driver_postgres = {
    "postgres",
    pg_open, pg_close,
    pg_read, pg_write, pg_del,
    pg_select_begin, pg_select_next, pg_select_end,
    pg_create, pg_remove,
    NULL,                                 /* names: TODO */
    NULL, NULL, NULL, NULL,               /* no native indexes (yet) */
    NULL, NULL,                           /* no lock authority (yet) */
    pg_map_ensure, pg_map_apply,          /* relational mapping */
};

const mvx_driver *mvx_driver_entry(int abi) {
    return abi == MVX_DRIVER_ABI ? &mvx_driver_postgres : NULL;
}
