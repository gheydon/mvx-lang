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

/* Account conversion between the git-legible directory form and live
 * hash files.  The directory form — NAME/ (records) beside NAME.DICT/
 * (dictionary) — is only ever what git tracks; a live account is hash
 * files with no .DICT.  mvx-convert-acct and mvx-git call these.
 *
 *   mvx_acct_import  directory form  ->  hash files  (clone / checkout)
 *   mvx_acct_export  hash files      ->  directory form  (commit)
 *
 * The heavy provisioning (cataloging BP, linking packages, creating
 * empty bulk files from their dictionaries) stays in the BUILD verb,
 * which import spawns once the files exist.
 */

#include "mvx_runtime.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- a small in-memory stash of {id, record} pairs -------------------- */
typedef struct { char *id; int64_t idlen; char *rec; int64_t reclen; } kv;
typedef struct { kv *v; size_t n, cap; } stash;

static void stash_add(stash *s, const char *id, int64_t idlen,
                      const char *rec, int64_t reclen) {
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->v = realloc(s->v, s->cap * sizeof *s->v);
        if (!s->v) mvx_fatal("out of memory converting account");
    }
    kv *e = &s->v[s->n++];
    e->idlen = idlen; e->reclen = reclen;
    e->id = malloc(idlen ? (size_t)idlen : 1);
    e->rec = malloc(reclen ? (size_t)reclen : 1);
    if (!e->id || !e->rec) mvx_fatal("out of memory converting account");
    memcpy(e->id, id, (size_t)idlen);
    memcpy(e->rec, rec, (size_t)reclen);
}
static void stash_free(stash *s) {
    for (size_t i = 0; i < s->n; i++) { free(s->v[i].id); free(s->v[i].rec); }
    free(s->v);
    s->v = NULL; s->n = s->cap = 0;
}

static void set(mv_value *v, const char *p, int64_t len) {
    mv_init(v); mv_set_str(v, p, len);
}

/* Read every record of an open file into the stash (optionally skipping
 * the %FILE% control record). */
static void drain(mvx_ctx *ctx, mv_value *f, stash *s, int skip_meta) {
    mvx_select(ctx, f);
    mv_value id; mv_init(&id);
    while (mvx_readnext(ctx, &id)) {
        char nb[64]; const char *ip;
        int64_t il = mv_val_chars(&id, nb, sizeof nb, &ip);
        if (skip_meta && il == 6 && memcmp(ip, "%FILE%", 6) == 0) continue;
        mv_value rec; mv_init(&rec);
        if (mvx_read(ctx, &rec, f, &id, 0)) {
            char rb[64]; const char *rp;
            int64_t rl = mv_val_chars(&rec, rb, sizeof rb, &rp);
            stash_add(s, ip, il, rp, rl);
        }
        mv_clear(&rec);
    }
    mv_clear(&id);
}

/* Write a stash into an open file. */
static void pour(mvx_ctx *ctx, mv_value *f, const stash *s) {
    for (size_t i = 0; i < s->n; i++) {
        mv_value id, rec;
        set(&id, s->v[i].id, s->v[i].idlen);
        set(&rec, s->v[i].rec, s->v[i].reclen);
        mvx_write(ctx, &rec, f, &id, 0, 0);
        mv_clear(&id); mv_clear(&rec);
    }
}

/* recursive rm of a directory (the directory form is disposable) */
static void rm_rf(const char *path) {
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char p[4096];
            snprintf(p, sizeof p, "%s/%s", path, e->d_name);
            struct stat sb;
            if (stat(p, &sb) == 0 && S_ISDIR(sb.st_mode)) rm_rf(p);
            else unlink(p);
        }
        closedir(d);
    }
    rmdir(path);
}

static const char *acct_dir(void) {
    const char *a = getenv("MVXACCOUNT");
    return (a && a[0]) ? a : ".";
}

/* Open the literal directory file `name` (data spec, not via the DICT
 * modifier) so a NAME.DICT sibling opens even when NAME has no data. */
static int open_lit(mvx_ctx *ctx, const char *name, mv_value *f) {
    mv_value spec; set(&spec, name, (int64_t)strlen(name));
    int64_t ok = mvx_open(ctx, NULL, &spec, f);
    mv_clear(&spec);
    return (int)ok;
}
static int open_dict(mvx_ctx *ctx, const char *name, mv_value *f) {
    mv_value dict, spec;
    set(&dict, "DICT", 4);
    set(&spec, name, (int64_t)strlen(name));
    int64_t ok = mvx_open(ctx, &dict, &spec, f);
    mv_clear(&dict); mv_clear(&spec);
    return (int)ok;
}

/* Read a directory file's %FILE% backend type.  Returns 1 if a %FILE%
 * record was found (type/conn filled), 0 otherwise (type left "lmdb"). */
static int file_type(mvx_ctx *ctx, const char *dictname, char *type,
                     size_t tcap, char *conn, size_t ccap) {
    snprintf(type, tcap, "lmdb");
    if (conn && ccap) conn[0] = '\0';
    mv_value f;
    if (!open_lit(ctx, dictname, &f)) return 0;
    int found = 0;
    mv_value id, rec; set(&id, "%FILE%", 6); mv_init(&rec);
    if (mvx_read(ctx, &rec, &f, &id, 0)) {
        char rb[64]; const char *rp;
        int64_t rl = mv_val_chars(&rec, rb, sizeof rb, &rp);
        /* open form: the bare portable class DIR or hash (no marks) */
        if (rl == 3 && strncasecmp(rp, "DIR", 3) == 0) {
            snprintf(type, tcap, "dir"); found = 1;
        } else if (rl == 4 && strncasecmp(rp, "hash", 4) == 0) {
            snprintf(type, tcap, "lmdb"); found = 1;   /* default hash backend */
        } else {
            /* legacy record is  FILE <VM> type <VM> conn ; marks are 0xFD */
            const char *p = rp, *end = rp + rl;
            const char *m1 = memchr(p, 0xFD, (size_t)(end - p));
            if (m1) {
                const char *t = m1 + 1;
                const char *m2 = memchr(t, 0xFD, (size_t)(end - t));
                const char *te = m2 ? m2 : end;
                snprintf(type, tcap, "%.*s", (int)(te - t), t);
                if (m2 && conn && ccap)
                    snprintf(conn, ccap, "%.*s", (int)(end - (m2 + 1)), m2 + 1);
                found = 1;
            }
        }
    }
    mv_clear(&id); mv_clear(&rec); mv_clear(&f);
    return found;
}

/* Guarantee a directory file's dictionary carries a %FILE% record — a
 * dir file must always have one so it is never mistaken for a hash file
 * (e.g. a NAME.DICT made by hand, or an older account). */
static void ensure_dir_meta(mvx_ctx *ctx, const char *nm) {
    mv_value f;
    if (!open_dict(ctx, nm, &f)) return;
    mv_value id, rec; set(&id, "%FILE%", 6); mv_init(&rec);
    if (!mvx_read(ctx, &rec, &f, &id, 0)) {
        char meta[8] = {'F', 'I', 'L', 'E', (char)0xFD, 'd', 'i', 'r'};
        mv_value v; set(&v, meta, sizeof meta);
        mvx_write(ctx, &v, &f, &id, 0, 0);
        mv_clear(&v);
    }
    mv_clear(&id); mv_clear(&rec); mv_clear(&f);
}

static int64_t createfile(mvx_ctx *ctx, const char *name, const char *type,
                          const char *conn) {
    mv_value spec; set(&spec, name, (int64_t)strlen(name));
    int64_t ok;
    if (!strcmp(type, "lmdb")) {
        ok = mvx_createfile(ctx, &spec, NULL);
    } else if (!strcmp(type, "dir")) {
        mv_value tv; set(&tv, "DIR", 3);
        ok = mvx_createfile(ctx, &spec, &tv);
        mv_clear(&tv);
    } else {
        char t[600];
        if (conn && conn[0]) snprintf(t, sizeof t, "USING %s %s", type, conn);
        else                 snprintf(t, sizeof t, "USING %s", type);
        mv_value tv; set(&tv, t, (int64_t)strlen(t));
        ok = mvx_createfile(ctx, &spec, &tv);
        mv_clear(&tv);
    }
    mv_clear(&spec);
    return ok;
}
/* spawn `mvx -a <acct> -c <verb>` with developer privilege */
static int run_verb(const char *verb) {
    const char *mvx = getenv("MVX");
    if (!mvx || !mvx[0]) mvx = "mvx";
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setenv("MVXPRIV", "developer", 1);
        char *argv[] = {(char *)mvx, "-a", (char *)acct_dir(),
                        "-c", (char *)verb, NULL};
        execvp(mvx, argv);
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0) { }
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Names that are infrastructure directories, not data files. */
static int infra(const char *n) {
    return !strcmp(n, "CATALOG") || !strcmp(n, "LIB") || !strcmp(n, "bin");
}

/* The account's master dictionary — VOC (Pick/UniVerse) or MD (classic).
 * It must be rebuilt before any other file, because creating a file
 * registers its pointer there. */
static int is_master(const char *n) {
    return !strcmp(n, "VOC") || !strcmp(n, "MD");
}

/* Make the directory an account: write the .mvx descriptor if it has none, so
 * is_account() holds for the rest of the rebuild.  Called right before the
 * master dictionary is created (VOC/MD leads the migration order).  BUILD
 * writes it too, as a fallback, and skips it when one is already present. */
static void ensure_account(const char *acct) {
    /* The on-disk account is always native MVX: its descriptor is `.mvx`.  The
       open account format's `.mv-account` lives only in git; a checkout of an
       open account lands it on disk transiently, so rebuilding into a native
       account renames it back to `.mvx`. */
    char pmvx[4096], popn[4096];
    snprintf(pmvx, sizeof pmvx, "%s/.mvx", acct);
    snprintf(popn, sizeof popn, "%s/.mv-account", acct);
    struct stat sb;
    if (stat(pmvx, &sb) == 0) return;            /* already a native account */
    if (stat(popn, &sb) == 0) {                  /* open checkout → native */
        rename(popn, pmvx);
        return;
    }
    char cwd[4096];
    const char *base = acct;
    if (!strcmp(acct, ".") && getcwd(cwd, sizeof cwd)) base = cwd;
    const char *slash = strrchr(base, '/');
    if (slash && slash[1]) base = slash + 1;
    FILE *f = fopen(pmvx, "w");
    if (!f) return;
    fprintf(f, "# MVX account descriptor\nname = %s\nversion = 1\n", base);
    fclose(f);
}
static int ends_dict(const char *n, size_t len) {
    return len > 5 && memcmp(n + len - 5, ".DICT", 5) == 0;
}

/* ---- import: directory form -> hash files ----------------------------- */
int mvx_acct_import(mvx_ctx *ctx) {
    const char *acct = acct_dir();
    /* Collect the base names of every NAME.DICT in the account first;
     * migrating mutates the directory as we go. */
    char (*names)[256] = NULL;
    size_t nn = 0, ncap = 0;
    DIR *d = opendir(acct);
    if (!d) { fprintf(stderr, "mvx-convert-acct: cannot open %s\n", acct); return 1; }
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (!ends_dict(e->d_name, l)) continue;
        char p[4096]; snprintf(p, sizeof p, "%s/%s", acct, e->d_name);
        struct stat sb;
        if (stat(p, &sb) != 0 || !S_ISDIR(sb.st_mode)) continue;
        if (nn == ncap) {
            ncap = ncap ? ncap * 2 : 32;
            names = realloc(names, ncap * sizeof *names);
            if (!names) mvx_fatal("out of memory converting account");
        }
        snprintf(names[nn], sizeof names[nn], "%.*s", (int)(l - 5), e->d_name);
        nn++;
    }
    closedir(d);

    /* Rebuild the master dictionary (VOC / MD) first: every createfile below
     * registers its file pointer there, so it must already exist — with its
     * own declared backend — before the rest are created.  readdir order is
     * otherwise arbitrary, so move any master name to the front. */
    size_t front = 0;
    for (size_t i = 0; i < nn; i++)
        if (is_master(names[i])) {
            if (i != front) {
                char tmp[256];
                memcpy(tmp, names[front], sizeof tmp);
                memcpy(names[front], names[i], sizeof names[front]);
                memcpy(names[i], tmp, sizeof names[i]);
            }
            front++;
        }

    /* Establish the account before rebuilding the master dictionary, so the
     * directory reads as an account for the files created after it. */
    ensure_account(acct);

    int nmig = 0;
    for (size_t i = 0; i < nn; i++) {
        const char *nm = names[i];
        if (infra(nm)) continue;

        char type[64], conn[512];
        char dictname[300];
        snprintf(dictname, sizeof dictname, "%s.DICT", nm);
        int found = file_type(ctx, dictname, type, sizeof type,
                              conn, sizeof conn);
        if (!found) {
            /* No %FILE%: a data directory on disk is a directory file
             * (never guess lmdb and convert it away). */
            char dp[4096];
            snprintf(dp, sizeof dp, "%s/%s", acct, nm);
            struct stat sb;
            if (stat(dp, &sb) == 0 && S_ISDIR(sb.st_mode))
                snprintf(type, sizeof type, "dir");
        }
        if (!strcmp(type, "dir")) {
            ensure_dir_meta(ctx, nm);         /* a dir file always has %FILE% */
            continue;
        }

        stash data = {0}, dict = {0};
        mv_value f;
        if (open_lit(ctx, nm, &f)) { drain(ctx, &f, &data, 0); mv_clear(&f); }
        if (open_lit(ctx, dictname, &f)) { drain(ctx, &f, &dict, 1); mv_clear(&f); }

        char p[4096];
        snprintf(p, sizeof p, "%s/%s", acct, nm);      rm_rf(p);
        snprintf(p, sizeof p, "%s/%s.DICT", acct, nm); rm_rf(p);

        if (!createfile(ctx, nm, type, conn)) {
            fprintf(stderr, "mvx-convert-acct: cannot create %s\n", nm);
            stash_free(&data); stash_free(&dict); continue;
        }
        if (open_lit(ctx, nm, &f))  { pour(ctx, &f, &data); mv_clear(&f); }
        if (open_dict(ctx, nm, &f)) { pour(ctx, &f, &dict); mv_clear(&f); }
        stash_free(&data); stash_free(&dict);
        nmig++;
    }
    free(names);

    printf("converted %d file(s) to hash files\n", nmig);
    /* BUILD provisions the rest: empty bulk files from lone dictionaries,
     * cataloged BP, linked packages, indexes, and the .mvx descriptor. */
    return run_verb("BUILD");
}

/* ---- export: hash files -> directory form ----------------------------
 * The inverse of import: every hash-backed file becomes NAME/ (records)
 * beside NAME.DICT/ (dictionary), copying the dictionary verbatim so its
 * %FILE% keeps the real backend type — that is what tells a later import
 * to rebuild it as a hash file.  Files that are already directory files
 * (dir backend, e.g. CATALOG) are left as they are. */
int mvx_acct_export(mvx_ctx *ctx) {
    mv_value fl; mv_init(&fl);
    mvx_filelist(ctx, &fl);
    char nb[64]; const char *fp;
    int64_t fllen = mv_val_chars(&fl, nb, sizeof nb, &fp);

    char (*names)[256] = NULL;
    size_t nn = 0, ncap = 0;
    const char *p = fp, *end = fp + fllen;
    while (p < end) {
        const char *am = memchr(p, (char)0xFE, (size_t)(end - p));
        const char *ee = am ? am : end;
        const char *vm = memchr(p, (char)0xFD, (size_t)(ee - p));
        const char *ne = vm ? vm : ee;
        char ty = (vm && vm + 1 < ee) ? vm[1] : 'L';
        if (ty != 'D' && ne > p) {             /* hash-backed file */
            if (nn == ncap) {
                ncap = ncap ? ncap * 2 : 32;
                names = realloc(names, ncap * sizeof *names);
                if (!names) mvx_fatal("out of memory converting account");
            }
            snprintf(names[nn], sizeof names[nn], "%.*s", (int)(ne - p), p);
            nn++;
        }
        p = am ? am + 1 : end;
    }
    mv_clear(&fl);

    int nexp = 0;
    for (size_t i = 0; i < nn; i++) {
        const char *nm = names[i];
        if (infra(nm)) continue;

        stash data = {0}, dict = {0};
        mv_value f;
        if (open_lit(ctx, nm, &f))  { drain(ctx, &f, &data, 0); mv_clear(&f); }
        if (open_dict(ctx, nm, &f)) { drain(ctx, &f, &dict, 0); mv_clear(&f); }

        char dirt[8]; snprintf(dirt, sizeof dirt, "dir");
        createfile(ctx, nm, dirt, NULL);       /* NAME/ + NAME.DICT */
        if (open_lit(ctx, nm, &f))  { pour(ctx, &f, &data); mv_clear(&f); }
        if (open_dict(ctx, nm, &f)) { pour(ctx, &f, &dict); mv_clear(&f); }
        stash_free(&data); stash_free(&dict);
        nexp++;
    }
    free(names);
    printf("exported %d file(s) to directory form\n", nexp);
    return 0;
}
