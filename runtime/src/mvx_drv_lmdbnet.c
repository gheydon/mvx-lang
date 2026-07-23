/* lmdbnet driver — the networked transport for LMDB-backed files.
 *
 * Presents the identical storage contract as the embedded driver, but
 * every operation travels to an mvxd daemon over one connection per
 * process.  $MVXDAEMON names the daemon: a path means a unix socket,
 * host:port means TCP.  Locks are held by the daemon against this
 * connection — process exit releases them (the lease).
 */
#include "../include/mvx_driver.h"
#include "../include/mvxd_proto.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct {
    mvx_file_base base;
} net_file;

struct mvx_cursor {
    mv_value *ids;
    int64_t n, pos;
};

static int g_sock = -1;

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

static int daemon_connect(char *err, size_t errlen) {
    if (g_sock >= 0) return 1;
    const char *spec = getenv("MVXDAEMON");
    if (!spec || !spec[0]) {
        snprintf(err, errlen, "lmdbnet: $MVXDAEMON is not set");
        return 0;
    }
    signal(SIGPIPE, SIG_IGN);
    int fd;
    if (strchr(spec, '/')) {            /* unix socket */
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un a = {0};
        a.sun_family = AF_UNIX;
        snprintf(a.sun_path, sizeof a.sun_path, "%s", spec);
        if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
            close(fd);
            snprintf(err, errlen, "lmdbnet: cannot reach daemon at %s",
                     spec);
            return 0;
        }
    } else {                            /* host:port */
        char host[256] = "127.0.0.1";
        int port = 0;
        const char *colon = strrchr(spec, ':');
        if (colon) {
            size_t hl = (size_t)(colon - spec);
            if (hl > 0 && hl < sizeof host) {
                memcpy(host, spec, hl);
                host[hl] = '\0';
            }
            port = atoi(colon + 1);
        }
        struct addrinfo hints = {0}, *res = NULL;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char ports[16];
        snprintf(ports, sizeof ports, "%d", port);
        if (getaddrinfo(host, ports, &hints, &res) != 0 || !res) {
            snprintf(err, errlen, "lmdbnet: cannot resolve %s", spec);
            return 0;
        }
        fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
            close(fd);
            freeaddrinfo(res);
            snprintf(err, errlen, "lmdbnet: cannot reach daemon at %s",
                     spec);
            return 0;
        }
        freeaddrinfo(res);
    }
    g_sock = fd;
    return 1;
}

/* ------------------------------------------------------- request builder */

typedef struct {
    char *d;
    size_t len, cap;
} reqbuf;

static void rput(reqbuf *b, const void *p, size_t n) {
    if (b->len + n > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        while (b->cap < b->len + n) b->cap *= 2;
        b->d = realloc(b->d, b->cap);
        if (!b->d) mvx_fatal("out of memory in lmdbnet");
    }
    memcpy(b->d + b->len, p, n);
    b->len += n;
}

static void r16(reqbuf *b, uint16_t v) { rput(b, &v, 2); }
static void r32(reqbuf *b, uint32_t v) { rput(b, &v, 4); }

static void rstr16(reqbuf *b, const char *p, size_t n) {
    r16(b, (uint16_t)n);
    rput(b, p, n);
}

/* Round trip: returns status, fills *resp/*rlen (caller frees). */
static int roundtrip(uint8_t op, reqbuf *b, char **resp, uint32_t *rlen) {
    *resp = NULL;
    *rlen = 0;
    if (g_sock < 0) {
        char err[128];
        if (!daemon_connect(err, sizeof err))
            mvx_fatal("%s", err);
    }
    uint32_t plen = (uint32_t)(1 + b->len);
    if (!send_all(g_sock, &plen, 4) || !send_all(g_sock, &op, 1) ||
        (b->len && !send_all(g_sock, b->d, b->len)))
        mvx_fatal("lmdbnet: daemon connection lost");
    uint32_t alen;
    uint8_t status;
    if (!recv_all(g_sock, &alen, 4) || alen < 1 ||
        !recv_all(g_sock, &status, 1))
        mvx_fatal("lmdbnet: daemon connection lost");
    if (alen > 1) {
        *resp = malloc(alen - 1);
        if (!*resp) mvx_fatal("out of memory in lmdbnet");
        if (!recv_all(g_sock, *resp, alen - 1))
            mvx_fatal("lmdbnet: daemon connection lost");
        *rlen = alen - 1;
    }
    free(b->d);
    b->d = NULL;
    b->len = b->cap = 0;
    return status;
}

/* ------------------------------------------------------------ contract */

static const mvx_driver mvx_driver_lmdbnet;

static mvx_file *net_open(const char *spec, char *err, size_t errlen) {
    if (!daemon_connect(err, errlen)) return NULL;
    reqbuf b = {0, 0, 0};
    rstr16(&b, spec, strlen(spec));
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_OPEN, &b, &resp, &rlen);
    free(resp);
    if (st != MVXD_ST_OK) return NULL;
    net_file *f = calloc(1, sizeof(net_file));
    if (!f) mvx_fatal("out of memory opening %s", spec);
    f->base.driver = &mvx_driver_lmdbnet;
    f->base.spec = strdup(spec);
    return (mvx_file *)f;
}

static void net_close(mvx_file *fh) {
    net_file *f = (net_file *)fh;
    free(f->base.spec);
    free(f);
}

static reqbuf spec_req(mvx_file *fh) {
    reqbuf b = {0, 0, 0};
    mvx_file_base *base = (mvx_file_base *)fh;
    rstr16(&b, base->spec, strlen(base->spec));
    return b;
}

static int net_read(mvx_file *fh, const char *id, int64_t idlen,
                    mv_value *rec) {
    reqbuf b = spec_req(fh);
    rstr16(&b, id, (size_t)idlen);
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_READ, &b, &resp, &rlen);
    if (st == MVXD_ST_OK && rlen >= 4) {
        uint32_t dl;
        memcpy(&dl, resp, 4);
        if (dl <= rlen - 4) mv_set_str(rec, resp + 4, dl);
    }
    free(resp);
    return st == MVXD_ST_OK;
}

static int net_write(mvx_file *fh, const char *id, int64_t idlen,
                     const mv_value *rec) {
    reqbuf b = spec_req(fh);
    rstr16(&b, id, (size_t)idlen);
    char nb[40];
    const char *rp;
    int64_t rl = mv_val_chars(rec, nb, sizeof nb, &rp);
    r32(&b, (uint32_t)rl);
    rput(&b, rp, (size_t)rl);
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_WRITE, &b, &resp, &rlen);
    free(resp);
    return st == MVXD_ST_OK;
}

static int net_del(mvx_file *fh, const char *id, int64_t idlen) {
    reqbuf b = spec_req(fh);
    rstr16(&b, id, (size_t)idlen);
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_DEL, &b, &resp, &rlen);
    free(resp);
    return st == MVXD_ST_OK;
}

/* Parse a count+ids response into a cursor. */
static mvx_cursor *ids_cursor(const char *resp, uint32_t rlen) {
    mvx_cursor *c = calloc(1, sizeof(mvx_cursor));
    if (!c) mvx_fatal("out of memory in lmdbnet select");
    if (!resp || rlen < 4) return c;
    uint32_t count;
    memcpy(&count, resp, 4);
    const char *p = resp + 4, *end = resp + rlen;
    c->ids = calloc(count ? count : 1, sizeof(mv_value));
    if (!c->ids) mvx_fatal("out of memory in lmdbnet select");
    for (uint32_t i = 0; i < count && p + 2 <= end; i++) {
        uint16_t l;
        memcpy(&l, p, 2);
        p += 2;
        if (p + l > end) break;
        mv_init(&c->ids[c->n]);
        mv_set_str(&c->ids[c->n], p, l);
        c->n++;
        p += l;
    }
    return c;
}

static mvx_cursor *net_select_begin(mvx_file *fh) {
    reqbuf b = spec_req(fh);
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_SELECT, &b, &resp, &rlen);
    mvx_cursor *c = st == MVXD_ST_OK ? ids_cursor(resp, rlen)
                                     : calloc(1, sizeof(mvx_cursor));
    free(resp);
    return c;
}

static int net_select_next(mvx_cursor *c, mv_value *id) {
    if (!c || c->pos >= c->n) return 0;
    mv_copy(id, &c->ids[c->pos++]);
    return 1;
}

static void net_select_end(mvx_cursor *c) {
    if (!c) return;
    for (int64_t i = 0; i < c->n; i++) mv_clear(&c->ids[i]);
    free(c->ids);
    free(c);
}

static int net_create(const char *spec, char *err, size_t errlen) {
    if (!daemon_connect(err, errlen)) return 0;
    reqbuf b = {0, 0, 0};
    rstr16(&b, spec, strlen(spec));
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_CREATE, &b, &resp, &rlen);
    free(resp);
    return st == MVXD_ST_OK;
}

static int net_remove(const char *spec, char *err, size_t errlen) {
    if (!daemon_connect(err, errlen)) return 0;
    reqbuf b = {0, 0, 0};
    rstr16(&b, spec, strlen(spec));
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_REMOVE, &b, &resp, &rlen);
    free(resp);
    return st == MVXD_ST_OK;
}

static int net_names(mv_value *out, char *err, size_t errlen) {
    if (!daemon_connect(err, errlen)) return 0;
    reqbuf b = {0, 0, 0};
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_NAMES, &b, &resp, &rlen);
    if (st != MVXD_ST_OK || !resp || rlen < 4) {
        free(resp);
        return 0;
    }
    uint32_t count;
    memcpy(&count, resp, 4);
    char *buf = NULL;
    size_t blen = 0, bcap = 0;
    const char *p = resp + 4, *end = resp + rlen;
    for (uint32_t i = 0; i < count && p + 2 <= end; i++) {
        uint16_t l;
        memcpy(&l, p, 2);
        p += 2;
        if (p + l > end) break;
        if (blen + l + 1 > bcap) {
            bcap = bcap ? bcap * 2 : 256;
            while (bcap < blen + l + 1) bcap *= 2;
            buf = realloc(buf, bcap);
            if (!buf) mvx_fatal("out of memory in lmdbnet names");
        }
        if (blen) buf[blen++] = (char)0xFE;
        memcpy(buf + blen, p, l);
        blen += l;
        p += l;
    }
    mv_set_str(out, buf ? buf : "", (int64_t)blen);
    free(buf);
    free(resp);
    return 1;
}

static int net_write_ix(mvx_file *fh, const char *id, int64_t idlen,
                        const mv_value *rec, const mvx_ixop *ops,
                        int nops) {
    reqbuf b = spec_req(fh);
    rstr16(&b, id, (size_t)idlen);
    char nb[40];
    const char *rp;
    int64_t rl = mv_val_chars(rec, nb, sizeof nb, &rp);
    r32(&b, (uint32_t)rl);
    rput(&b, rp, (size_t)rl);
    r16(&b, (uint16_t)nops);
    for (int i = 0; i < nops; i++) {
        rstr16(&b, ops[i].item, strlen(ops[i].item));
        rstr16(&b, ops[i].key, (size_t)ops[i].klen);
        uint8_t add = ops[i].add ? 1 : 0;
        rput(&b, &add, 1);
    }
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_WRITE_IX, &b, &resp, &rlen);
    free(resp);
    return st == MVXD_ST_OK;
}

static int net_del_ix(mvx_file *fh, const char *id, int64_t idlen,
                      const mvx_ixop *ops, int nops) {
    reqbuf b = spec_req(fh);
    rstr16(&b, id, (size_t)idlen);
    r16(&b, (uint16_t)nops);
    for (int i = 0; i < nops; i++) {
        rstr16(&b, ops[i].item, strlen(ops[i].item));
        rstr16(&b, ops[i].key, (size_t)ops[i].klen);
        uint8_t add = 0;
        rput(&b, &add, 1);
    }
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_DEL_IX, &b, &resp, &rlen);
    free(resp);
    return st == MVXD_ST_OK;
}

static mvx_cursor *net_index_select(mvx_file *fh, const char *item,
                                    const char *key, int64_t klen) {
    reqbuf b = spec_req(fh);
    rstr16(&b, item, strlen(item));
    rstr16(&b, key, (size_t)klen);
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_IDX_SELECT, &b, &resp, &rlen);
    if (st != MVXD_ST_OK) {             /* no such index */
        free(resp);
        return NULL;
    }
    mvx_cursor *c = ids_cursor(resp, rlen);
    free(resp);
    return c;
}

static int net_index_drop(mvx_file *fh, const char *item) {
    reqbuf b = spec_req(fh);
    rstr16(&b, item, strlen(item));
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_IDX_DROP, &b, &resp, &rlen);
    free(resp);
    return st == MVXD_ST_OK;
}

static int net_lock(mvx_file *fh, const char *id, int64_t idlen) {
    reqbuf b = spec_req(fh);
    rstr16(&b, id, (size_t)idlen);
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_LOCK, &b, &resp, &rlen);
    free(resp);
    return st == MVXD_ST_OK;
}

static int net_unlock(mvx_file *fh, const char *id, int64_t idlen) {
    reqbuf b = spec_req(fh);
    rstr16(&b, id, (size_t)idlen);
    char *resp;
    uint32_t rlen;
    int st = roundtrip(MVXD_OP_UNLOCK, &b, &resp, &rlen);
    free(resp);
    return st == MVXD_ST_OK;
}

static const mvx_driver mvx_driver_lmdbnet = {
    "lmdbnet",
    net_open, net_close,
    net_read, net_write, net_del,
    net_select_begin, net_select_next, net_select_end,
    net_create, net_remove,
    net_names,
    net_write_ix, net_del_ix, net_index_select, net_index_drop,
    net_lock, net_unlock,
};

const mvx_driver *mvx_driver_entry(int abi) {
    return abi == MVX_DRIVER_ABI ? &mvx_driver_lmdbnet : NULL;
}
