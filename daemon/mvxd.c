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

/* mvxd — the networked LMDB daemon (ARCHITECTURE.md 4.3).
 *
 * Owns one LMDB environment EXCLUSIVELY and serialises client access:
 * a file is either embedded-access or daemon-owned, never both.  The
 * daemon is the single lock authority for its files; locks are leased
 * to the client connection and reaped when it drops, so a killed pod
 * cannot orphan a record lock.  SELECT snapshots the id list inside a
 * short transaction and only then sends it — a slow client never pins
 * a read transaction.
 *
 *   mvxd -d <datadir> (-s <unix-socket> | -p <port>)
 *
 * The daemon speaks raw record bytes; MV semantics live in the client
 * runtime.  Requests are handled one at a time — writes serialise
 * naturally, matching LMDB's single-writer design.
 */
#include "../runtime/include/mvxd_proto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <lmdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_CONNS 64
#define MAX_FRAME (16u * 1024 * 1024)

static MDB_env *g_env;

typedef struct lockent {
    char *key;                          /* spec \x01 id */
    int fd;
    struct lockent *next;
} lockent;

static lockent *g_locks;

/* ------------------------------------------------------------ plumbing */

static int recv_all(int fd, void *buf, size_t n) {
    char *p = buf;
    while (n > 0) {
        ssize_t r = recv(fd, p, n, 0);
        if (r <= 0) return 0;
        p += r;
        n -= (size_t)r;
    }
    return 1;
}

static int send_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    while (n > 0) {
        ssize_t r = send(fd, p, n, 0);
        if (r <= 0) return 0;
        p += r;
        n -= (size_t)r;
    }
    return 1;
}

typedef struct {
    char *d;
    size_t len, cap;
} outbuf;

static void oput(outbuf *o, const void *p, size_t n) {
    if (o->len + n > o->cap) {
        o->cap = o->cap ? o->cap * 2 : 256;
        while (o->cap < o->len + n) o->cap *= 2;
        o->d = realloc(o->d, o->cap);
        if (!o->d) exit(70);
    }
    memcpy(o->d + o->len, p, n);
    o->len += n;
}

static void o16(outbuf *o, uint16_t v) { oput(o, &v, 2); }
static void o32(outbuf *o, uint32_t v) { oput(o, &v, 4); }

/* Cursor over the request payload. */
typedef struct {
    const char *p;
    size_t left;
    int bad;
} inbuf;

static const char *itake(inbuf *i, size_t n) {
    if (i->left < n) {
        i->bad = 1;
        return NULL;
    }
    const char *p = i->p;
    i->p += n;
    i->left -= n;
    return p;
}

static uint16_t i16(inbuf *i) {
    const char *p = itake(i, 2);
    uint16_t v = 0;
    if (p) memcpy(&v, p, 2);
    return v;
}

static uint32_t i32(inbuf *i) {
    const char *p = itake(i, 4);
    uint32_t v = 0;
    if (p) memcpy(&v, p, 4);
    return v;
}

static const char *istr16(inbuf *i, uint16_t *len) {
    *len = i16(i);
    return itake(i, *len);
}

/* -------------------------------------------------------------- locks */

static char *lock_key(const char *spec, uint16_t sl, const char *id,
                      uint16_t il) {
    char *k = malloc((size_t)sl + 1 + il + 1);
    if (!k) exit(70);
    memcpy(k, spec, sl);
    k[sl] = '\x01';
    memcpy(k + sl + 1, id, il);
    k[sl + 1 + il] = '\0';
    return k;
}

static int lock_try(const char *key, int fd) {
    for (lockent *l = g_locks; l; l = l->next)
        if (strcmp(l->key, key) == 0)
            return l->fd == fd;         /* re-lock by owner is fine */
    lockent *l = malloc(sizeof(lockent));
    if (!l) exit(70);
    l->key = strdup(key);
    l->fd = fd;
    l->next = g_locks;
    g_locks = l;
    return 1;
}

static void lock_release(const char *key, int fd) {
    for (lockent **pp = &g_locks; *pp; pp = &(*pp)->next)
        if (strcmp((*pp)->key, key) == 0 && (*pp)->fd == fd) {
            lockent *dead = *pp;
            *pp = dead->next;
            free(dead->key);
            free(dead);
            return;
        }
}

/* The lease: a dropped connection releases everything it held. */
static void conn_reap(int fd) {
    lockent **pp = &g_locks;
    while (*pp) {
        if ((*pp)->fd == fd) {
            lockent *dead = *pp;
            *pp = dead->next;
            free(dead->key);
            free(dead);
        } else {
            pp = &(*pp)->next;
        }
    }
}

/* ---------------------------------------------------------------- lmdb */

static int dbi_of(MDB_txn *txn, const char *spec, uint16_t sl,
                  unsigned flags, MDB_dbi *dbi) {
    char name[1400];
    if (sl >= sizeof name) return MDB_NOTFOUND;
    memcpy(name, spec, sl);
    name[sl] = '\0';
    return mdb_dbi_open(txn, name, flags, dbi);
}

static int idx_dbi_of(MDB_txn *txn, const char *spec, uint16_t sl,
                      const char *item, uint16_t il, unsigned extra,
                      MDB_dbi *dbi) {
    char name[1400];
    if ((size_t)sl + il + 6 >= sizeof name) return MDB_NOTFOUND;
    memcpy(name, spec, sl);
    memcpy(name + sl, ".IDX.", 5);
    memcpy(name + sl + 5, item, il);
    name[sl + 5 + il] = '\0';
    return mdb_dbi_open(txn, name, MDB_DUPSORT | extra, dbi);
}

/* ------------------------------------------------------------ handlers */

static void handle(int fd, uint8_t op, inbuf *in, outbuf *out,
                   uint8_t *status) {
    *status = MVXD_ST_ERR;
    uint16_t sl = 0, il = 0;
    const char *spec = NULL;

    if (op != MVXD_OP_NAMES) {
        spec = istr16(in, &sl);
        if (in->bad) return;
    }

    MDB_txn *txn = NULL;
    MDB_dbi dbi;

    switch (op) {
    case MVXD_OP_OPEN: {
        if (mdb_txn_begin(g_env, NULL, MDB_RDONLY, &txn)) return;
        *status = dbi_of(txn, spec, sl, 0, &dbi) == 0 ? MVXD_ST_OK
                                                      : MVXD_ST_NO;
        mdb_txn_abort(txn);
        return;
    }
    case MVXD_OP_CREATE: {
        if (mdb_txn_begin(g_env, NULL, 0, &txn)) return;
        if (dbi_of(txn, spec, sl, 0, &dbi) == 0) {
            mdb_txn_abort(txn);
            *status = MVXD_ST_NO;       /* already exists */
            return;
        }
        mdb_txn_abort(txn);
        if (mdb_txn_begin(g_env, NULL, 0, &txn)) return;
        if (dbi_of(txn, spec, sl, MDB_CREATE, &dbi) == 0 &&
            mdb_txn_commit(txn) == 0)
            *status = MVXD_ST_OK;
        else
            mdb_txn_abort(txn);
        return;
    }
    case MVXD_OP_REMOVE: {
        if (mdb_txn_begin(g_env, NULL, 0, &txn)) return;
        if (dbi_of(txn, spec, sl, 0, &dbi) != 0) {
            mdb_txn_abort(txn);
            *status = MVXD_ST_NO;
            return;
        }
        if (mdb_drop(txn, dbi, 1) == 0 && mdb_txn_commit(txn) == 0)
            *status = MVXD_ST_OK;
        else
            mdb_txn_abort(txn);
        return;
    }
    case MVXD_OP_READ: {
        const char *id = istr16(in, &il);
        if (in->bad) return;
        if (mdb_txn_begin(g_env, NULL, MDB_RDONLY, &txn)) return;
        if (dbi_of(txn, spec, sl, 0, &dbi) != 0) {
            mdb_txn_abort(txn);
            *status = MVXD_ST_NO;
            return;
        }
        MDB_val k = {il, (void *)id}, v;
        if (mdb_get(txn, dbi, &k, &v) == 0) {
            o32(out, (uint32_t)v.mv_size);
            oput(out, v.mv_data, v.mv_size);   /* copy out, then abort */
            *status = MVXD_ST_OK;
        } else {
            *status = MVXD_ST_NO;
        }
        mdb_txn_abort(txn);
        return;
    }
    case MVXD_OP_WRITE:
    case MVXD_OP_WRITE_IX: {
        const char *id = istr16(in, &il);
        if (in->bad) return;
        uint32_t dl = i32(in);
        const char *data = itake(in, dl);
        if (in->bad) return;
        if (mdb_txn_begin(g_env, NULL, 0, &txn)) return;
        if (dbi_of(txn, spec, sl, 0, &dbi) != 0) {
            mdb_txn_abort(txn);
            *status = MVXD_ST_NO;
            return;
        }
        if (op == MVXD_OP_WRITE_IX) {
            uint16_t nops = i16(in);
            for (uint16_t k = 0; k < nops; k++) {
                uint16_t itl, kl;
                const char *item = istr16(in, &itl);
                const char *key = istr16(in, &kl);
                const char *addp = itake(in, 1);
                if (in->bad) {
                    mdb_txn_abort(txn);
                    return;
                }
                MDB_dbi xdbi;
                if (idx_dbi_of(txn, spec, sl, item, itl, MDB_CREATE,
                               &xdbi) != 0) {
                    mdb_txn_abort(txn);
                    return;
                }
                MDB_val xk = {kl, (void *)key}, xd = {il, (void *)id};
                if (*addp) {
                    int rc = mdb_put(txn, xdbi, &xk, &xd, 0);
                    if (rc != 0 && rc != MDB_KEYEXIST) {
                        mdb_txn_abort(txn);
                        return;
                    }
                } else {
                    int rc = mdb_del(txn, xdbi, &xk, &xd);
                    if (rc != 0 && rc != MDB_NOTFOUND) {
                        mdb_txn_abort(txn);
                        return;
                    }
                }
            }
        }
        MDB_val k = {il, (void *)id}, v = {dl, (void *)data};
        if (mdb_put(txn, dbi, &k, &v, 0) == 0 && mdb_txn_commit(txn) == 0)
            *status = MVXD_ST_OK;
        else
            mdb_txn_abort(txn);
        return;
    }
    case MVXD_OP_DEL:
    case MVXD_OP_DEL_IX: {
        const char *id = istr16(in, &il);
        if (in->bad) return;
        if (mdb_txn_begin(g_env, NULL, 0, &txn)) return;
        if (dbi_of(txn, spec, sl, 0, &dbi) != 0) {
            mdb_txn_abort(txn);
            *status = MVXD_ST_NO;
            return;
        }
        if (op == MVXD_OP_DEL_IX) {
            uint16_t nops = i16(in);
            for (uint16_t k = 0; k < nops; k++) {
                uint16_t itl, kl;
                const char *item = istr16(in, &itl);
                const char *key = istr16(in, &kl);
                const char *addp = itake(in, 1);
                (void)addp;
                if (in->bad) {
                    mdb_txn_abort(txn);
                    return;
                }
                MDB_dbi xdbi;
                if (idx_dbi_of(txn, spec, sl, item, itl, MDB_CREATE,
                               &xdbi) != 0)
                    continue;
                MDB_val xk = {kl, (void *)key}, xd = {il, (void *)id};
                int rc = mdb_del(txn, xdbi, &xk, &xd);
                if (rc != 0 && rc != MDB_NOTFOUND) {
                    mdb_txn_abort(txn);
                    return;
                }
            }
        }
        MDB_val k = {il, (void *)id};
        int rc = mdb_del(txn, dbi, &k, NULL);
        if (rc != 0 && rc != MDB_NOTFOUND) {
            mdb_txn_abort(txn);
            return;
        }
        if (mdb_txn_commit(txn) == 0)
            *status = rc == 0 ? MVXD_ST_OK : MVXD_ST_NO;
        return;
    }
    case MVXD_OP_SELECT: {
        if (mdb_txn_begin(g_env, NULL, MDB_RDONLY, &txn)) return;
        if (dbi_of(txn, spec, sl, 0, &dbi) != 0) {
            mdb_txn_abort(txn);
            *status = MVXD_ST_NO;
            return;
        }
        MDB_cursor *cur;
        uint32_t count = 0;
        size_t count_pos = out->len;
        o32(out, 0);
        if (mdb_cursor_open(txn, dbi, &cur) == 0) {
            MDB_val k, v;
            while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == 0) {
                o16(out, (uint16_t)k.mv_size);
                oput(out, k.mv_data, k.mv_size);
                count++;
            }
            mdb_cursor_close(cur);
        }
        mdb_txn_abort(txn);             /* snapshot complete before send */
        memcpy(out->d + count_pos, &count, 4);
        *status = MVXD_ST_OK;
        return;
    }
    case MVXD_OP_IDX_SELECT: {
        uint16_t itl, kl;
        const char *item = istr16(in, &itl);
        const char *key = istr16(in, &kl);
        if (in->bad) return;
        if (mdb_txn_begin(g_env, NULL, MDB_RDONLY, &txn)) return;
        MDB_dbi xdbi;
        if (idx_dbi_of(txn, spec, sl, item, itl, 0, &xdbi) != 0) {
            mdb_txn_abort(txn);
            *status = MVXD_ST_NO;       /* no such index */
            return;
        }
        MDB_cursor *cur;
        uint32_t count = 0;
        size_t count_pos = out->len;
        o32(out, 0);
        if (mdb_cursor_open(txn, xdbi, &cur) == 0) {
            MDB_val k = {kl, (void *)key}, d;
            if (mdb_cursor_get(cur, &k, &d, MDB_SET_KEY) == 0) {
                do {
                    o16(out, (uint16_t)d.mv_size);
                    oput(out, d.mv_data, d.mv_size);
                    count++;
                } while (mdb_cursor_get(cur, &k, &d, MDB_NEXT_DUP) == 0);
            }
            mdb_cursor_close(cur);
        }
        mdb_txn_abort(txn);
        memcpy(out->d + count_pos, &count, 4);
        *status = MVXD_ST_OK;
        return;
    }
    case MVXD_OP_IDX_DROP: {
        uint16_t itl;
        const char *item = istr16(in, &itl);
        if (in->bad) return;
        if (mdb_txn_begin(g_env, NULL, 0, &txn)) return;
        MDB_dbi xdbi;
        if (idx_dbi_of(txn, spec, sl, item, itl, 0, &xdbi) != 0) {
            mdb_txn_abort(txn);
            *status = MVXD_ST_NO;
            return;
        }
        if (mdb_drop(txn, xdbi, 1) == 0 && mdb_txn_commit(txn) == 0)
            *status = MVXD_ST_OK;
        else
            mdb_txn_abort(txn);
        return;
    }
    case MVXD_OP_NAMES: {
        if (mdb_txn_begin(g_env, NULL, MDB_RDONLY, &txn)) return;
        MDB_dbi main_dbi;
        if (mdb_dbi_open(txn, NULL, 0, &main_dbi) != 0) {
            mdb_txn_abort(txn);
            return;
        }
        MDB_cursor *cur;
        uint32_t count = 0;
        size_t count_pos = out->len;
        o32(out, 0);
        if (mdb_cursor_open(txn, main_dbi, &cur) == 0) {
            MDB_val k, v;
            while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == 0) {
                o16(out, (uint16_t)k.mv_size);
                oput(out, k.mv_data, k.mv_size);
                count++;
            }
            mdb_cursor_close(cur);
        }
        mdb_txn_abort(txn);
        memcpy(out->d + count_pos, &count, 4);
        *status = MVXD_ST_OK;
        return;
    }
    case MVXD_OP_LOCK:
    case MVXD_OP_UNLOCK: {
        const char *id = istr16(in, &il);
        if (in->bad) return;
        char *key = lock_key(spec, sl, id, il);
        if (op == MVXD_OP_LOCK)
            *status = lock_try(key, fd) ? MVXD_ST_OK : MVXD_ST_BUSY;
        else {
            lock_release(key, fd);
            *status = MVXD_ST_OK;
        }
        free(key);
        return;
    }
    }
}

/* ---------------------------------------------------------------- main */

int main(int argc, char **argv) {
    const char *datadir = NULL, *sockpath = NULL;
    int port = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
            datadir = argv[++i];
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
            sockpath = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            port = atoi(argv[++i]);
        else {
            fprintf(stderr,
                    "usage: mvxd -d datadir (-s unix-socket | -p port)\n");
            return 2;
        }
    }
    if (!datadir || (!sockpath && port == 0)) {
        fprintf(stderr,
                "usage: mvxd -d datadir (-s unix-socket | -p port)\n");
        return 2;
    }

    signal(SIGPIPE, SIG_IGN);

    char envpath[4096];
    snprintf(envpath, sizeof envpath, "%s/mvxdata.lmdb", datadir);
    mkdir(datadir, 0775);
    mkdir(envpath, 0775);
    if (mdb_env_create(&g_env) || mdb_env_set_maxdbs(g_env, 126) ||
        mdb_env_set_mapsize(g_env, (size_t)1 << 30) ||
        mdb_env_open(g_env, envpath, 0, 0664)) {
        fprintf(stderr, "mvxd: cannot open environment at %s\n", envpath);
        return 1;
    }
    int dead = 0;
    mdb_reader_check(g_env, &dead);

    int lfd;
    if (sockpath) {
        lfd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un a = {0};
        a.sun_family = AF_UNIX;
        snprintf(a.sun_path, sizeof a.sun_path, "%s", sockpath);
        unlink(sockpath);
        if (bind(lfd, (struct sockaddr *)&a, sizeof a) != 0 ||
            listen(lfd, 16) != 0) {
            fprintf(stderr, "mvxd: cannot listen on %s\n", sockpath);
            return 1;
        }
    } else {
        lfd = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        struct sockaddr_in a = {0};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((uint16_t)port);
        if (bind(lfd, (struct sockaddr *)&a, sizeof a) != 0 ||
            listen(lfd, 16) != 0) {
            fprintf(stderr, "mvxd: cannot listen on port %d\n", port);
            return 1;
        }
    }
    fprintf(stderr, "mvxd: serving %s on %s\n", envpath,
            sockpath ? sockpath : "tcp");

    struct pollfd fds[MAX_CONNS + 1];
    int nfds = 1;
    fds[0].fd = lfd;
    fds[0].events = POLLIN;

    for (;;) {
        if (poll(fds, (nfds_t)nfds, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[0].revents & POLLIN) {
            int c = accept(lfd, NULL, NULL);
            if (c >= 0) {
                if (nfds <= MAX_CONNS) {
                    fds[nfds].fd = c;
                    fds[nfds].events = POLLIN;
                    nfds++;
                } else {
                    close(c);
                }
            }
        }
        for (int i = 1; i < nfds; i++) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;
            int fd = fds[i].fd;
            uint32_t plen;
            uint8_t op;
            char *payload = NULL;
            int ok = recv_all(fd, &plen, 4) && plen >= 1 &&
                     plen <= MAX_FRAME && recv_all(fd, &op, 1);
            if (ok && plen > 1) {
                payload = malloc(plen - 1);
                ok = payload && recv_all(fd, payload, plen - 1);
            }
            if (!ok) {
                conn_reap(fd);
                close(fd);
                fds[i] = fds[nfds - 1];
                nfds--;
                i--;
                free(payload);
                continue;
            }
            inbuf in = {payload, plen - 1, 0};
            outbuf out = {0, 0, 0};
            uint8_t status;
            handle(fd, op, &in, &out, &status);
            uint32_t rlen = (uint32_t)(1 + out.len);
            if (!send_all(fd, &rlen, 4) || !send_all(fd, &status, 1) ||
                (out.len && !send_all(fd, out.d, out.len))) {
                conn_reap(fd);
                close(fd);
                fds[i] = fds[nfds - 1];
                nfds--;
                i--;
            }
            free(out.d);
            free(payload);
        }
    }
    return 0;
}
