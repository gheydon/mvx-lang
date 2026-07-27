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

/* HTTP client as a language-extension package (#68), on the mvx_ext ABI.
 *
 * Adds two BASIC-callable functions — the transport the mv package manager
 * uses on MVX to reach the registry and pull release tarballs:
 *   HTTPGET(url)          -> the response body (binary-safe; "" on error)
 *   HTTPGETFILE(url,path) -> the HTTP status (-1 connect error, -2 write
 *                            error), writing a 2xx body to `path`
 *
 * Plain HTTP/1.1 over POSIX sockets, so the package has no external
 * dependency and builds/loads everywhere the runtime does.  HTTPS (TLS) is a
 * follow-up.  HTTPGET composes with JSONDECODE (the json package) for registry
 * metadata.
 */
#include "mvx_ext.h"

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct { char *p; size_t len, cap; } buf;

static void bappend(buf *b, const char *s, size_t n) {
    if (b->len + n > b->cap) {
        size_t c = b->cap ? b->cap : 4096;
        while (c < b->len + n) c *= 2;
        char *np = realloc(b->p, c);
        if (!np) return;
        b->p = np;
        b->cap = c;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
}

/* Parse "http://host[:port][/path]".  A non-http scheme (e.g. https) is
   rejected.  Returns 0 on success. */
static int parse_url(const char *url, char *host, size_t hcap, char *port,
                     size_t pcap, char *path, size_t pathcap) {
    if (strncmp(url, "http://", 7) != 0) return -1;
    const char *p = url + 7, *hs = p;
    while (*p && *p != ':' && *p != '/') p++;
    size_t hl = (size_t)(p - hs);
    if (hl == 0 || hl >= hcap) return -1;
    memcpy(host, hs, hl);
    host[hl] = '\0';
    snprintf(port, pcap, "80");
    if (*p == ':') {
        const char *ps = ++p;
        while (*p && *p != '/') p++;
        size_t pl = (size_t)(p - ps);
        if (pl == 0 || pl >= pcap) return -1;
        memcpy(port, ps, pl);
        port[pl] = '\0';
    }
    snprintf(path, pathcap, "%s", *p ? p : "/");
    return 0;
}

/* GET `url`.  On success returns 0, sets *status to the HTTP status code and
   out/blen to the malloc'd (binary-safe) response body.  Returns -1 on a URL,
   DNS, connect, or I/O error. */
static int http_get(const char *url, char **out, size_t *blen, int *status) {
    *out = NULL;
    *blen = 0;
    *status = 0;
    char host[256], port[16], path[2048];
    if (parse_url(url, host, sizeof host, port, sizeof port, path,
                  sizeof path) != 0)
        return -1;

    struct addrinfo hints, *res = NULL, *ai;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;
    int fd = -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return -1;

    char req[3072];
    int rl = snprintf(req, sizeof req,
                      "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: mvx-http/1.0"
                      "\r\nAccept: */*\r\nConnection: close\r\n\r\n", path, host);
    for (int off = 0; off < rl;) {
        ssize_t w = send(fd, req + off, (size_t)(rl - off), 0);
        if (w <= 0) { close(fd); return -1; }
        off += (int)w;
    }

    buf raw = {0, 0, 0};
    char tmp[8192];
    ssize_t r;
    while ((r = recv(fd, tmp, sizeof tmp, 0)) > 0) bappend(&raw, tmp, (size_t)r);
    close(fd);
    if (!raw.p) return -1;

    if (raw.len > 12 && strncmp(raw.p, "HTTP/", 5) == 0) {
        const char *sp = memchr(raw.p, ' ', raw.len);
        if (sp) *status = atoi(sp + 1);
    }
    const char *body = NULL;
    size_t bodylen = 0, hlen = 0;
    for (size_t i = 0; i + 3 < raw.len; i++)
        if (memcmp(raw.p + i, "\r\n\r\n", 4) == 0) {
            hlen = i;
            body = raw.p + i + 4;
            bodylen = raw.len - (i + 4);
            break;
        }
    if (!body) { free(raw.p); return -1; }

    int chunked = 0;
    for (size_t i = 0; i + 7 <= hlen; i++)
        if (strncasecmp(raw.p + i, "chunked", 7) == 0) { chunked = 1; break; }

    if (chunked) {
        buf b = {0, 0, 0};
        const char *p = body, *end = body + bodylen;
        while (p < end) {
            char *nl = memchr(p, '\n', (size_t)(end - p));
            if (!nl) break;
            long sz = strtol(p, NULL, 16);
            p = nl + 1;
            if (sz <= 0 || p + sz > end) break;
            bappend(&b, p, (size_t)sz);
            p += sz;
            if (p + 2 <= end && p[0] == '\r' && p[1] == '\n') p += 2;
        }
        free(raw.p);
        *out = b.p ? b.p : calloc(1, 1);
        *blen = b.len;
    } else {
        char *cp = malloc(bodylen ? bodylen : 1);
        if (cp && bodylen) memcpy(cp, body, bodylen);
        free(raw.p);
        *out = cp;
        *blen = bodylen;
    }
    return 0;
}

static int64_t arg_str(mv_value *v, char *dst, size_t cap) {
    char nb[40];
    const char *p;
    int64_t n = mv_val_chars(v, nb, sizeof nb, &p);
    if (n >= (int64_t)cap) n = (int64_t)cap - 1;
    memcpy(dst, p, (size_t)n);
    dst[n] = '\0';
    return n;
}

/* HTTPGET(url) -> the response body on a 2xx status; "" otherwise (a non-2xx
   status, or a connection error) so a caller can treat empty as "not found". */
static void ext_httpget(mvx_ctx *ctx, mv_value *ret, int32_t argc,
                        mv_value **argv) {
    (void)ctx;
    (void)argc;
    char url[2048];
    arg_str(argv[0], url, sizeof url);
    char *body;
    size_t blen;
    int status;
    if (http_get(url, &body, &blen, &status) == 0 && body &&
        status >= 200 && status < 300) {
        mv_set_str(ret, body, (int64_t)blen);
    } else {
        mv_set_str(ret, "", 0);
    }
    free(body);
}

/* HTTPGETFILE(url, path) -> HTTP status (-1 connect, -2 write); a 2xx body is
   written to `path`. */
static void ext_httpgetfile(mvx_ctx *ctx, mv_value *ret, int32_t argc,
                            mv_value **argv) {
    (void)ctx;
    (void)argc;
    char url[2048], path[2048];
    arg_str(argv[0], url, sizeof url);
    arg_str(argv[1], path, sizeof path);
    char *body;
    size_t blen;
    int status;
    char num[16];
    if (http_get(url, &body, &blen, &status) == 0) {
        if (status >= 200 && status < 300) {
            FILE *f = fopen(path, "wb");
            if (f) {
                if (blen) fwrite(body, 1, blen, f);
                fclose(f);
            } else {
                status = -2;
            }
        }
        free(body);
        snprintf(num, sizeof num, "%d", status);
    } else {
        snprintf(num, sizeof num, "-1");
    }
    mv_set_str(ret, num, (int64_t)strlen(num));
}

static const mvx_extfn http_fns[] = {
    {"HTTPGET", 1, 1, ext_httpget},
    {"HTTPGETFILE", 2, 2, ext_httpgetfile},
};
static const mvx_ext http_ext = {"http", 2, http_fns};

const mvx_ext *mvx_ext_entry(int abi) {
    return abi == MVX_EXT_ABI ? &http_ext : NULL;
}
