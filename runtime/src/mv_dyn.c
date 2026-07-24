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

/* Dynamic-array operations: extract / replace / insert / delete /
 * locate over attribute (0xFE), value (0xFD), and subvalue (0xFC)
 * marks, plus LEN / COUNT / DCOUNT.
 */
#include "mvx_runtime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AM ((char)0xFE)
#define VM ((char)0xFD)
#define SM ((char)0xFC)

typedef struct { const char *p; int64_t len; } span;

/* String view of a value; numeric tags render into buf. */
static span val_span(const mv_value *v, char *buf, size_t cap) {
    switch (v->tag) {
    case MV_STR:
        return (span){v->s->data, v->s->len};
    case MV_INT:
        return (span){buf,
                      (int64_t)snprintf(buf, cap, "%lld", (long long)v->i)};
    case MV_DBL: {
        double d = v->d;
        int64_t n;
        if (d == (double)(int64_t)d && d >= -1e15 && d <= 1e15)
            n = (int64_t)snprintf(buf, cap, "%lld", (long long)d);
        else {
            n = (int64_t)snprintf(buf, cap, "%.4f", d);
            while (n > 0 && buf[n - 1] == '0') buf[--n] = '\0';
            if (n > 0 && buf[n - 1] == '.') buf[--n] = '\0';
        }
        return (span){buf, n};
    }
    default:
        return (span){"", 0};
    }
}

static int64_t span_count(span s, char mark) {
    if (s.len == 0) return 0;
    int64_t n = 1;
    for (int64_t i = 0; i < s.len; i++)
        if (s.p[i] == mark) n++;
    return n;
}

/* Span of the idx-th (1-based) mark-separated element; empty span at the
   end when idx is past the last element. */
static span nth_span(span s, char mark, int64_t idx) {
    const char *end = s.p + s.len;
    const char *q = s.p;
    int64_t n = 1;
    while (n < idx) {
        const char *m = memchr(q, mark, (size_t)(end - q));
        if (!m) return (span){end, 0};
        q = m + 1;
        n++;
    }
    const char *m = memchr(q, mark, (size_t)(end - q));
    return (span){q, (m ? m : end) - q};
}

void mv_extract_fn(mv_value *dst, const mv_value *src, int64_t a,
                   int64_t v, int64_t s) {
    char nb[40];
    span sp = val_span(src, nb, sizeof nb);
    if (a > 0) sp = nth_span(sp, AM, a);
    if (v > 0) sp = nth_span(sp, VM, v);
    if (s > 0) sp = nth_span(sp, SM, s);
    mv_set_str(dst, sp.p, sp.len);
}

/* ------------------------------------------------------- modify engine */

typedef struct { char *d; int64_t len, cap; } dbuf;

static void bput(dbuf *b, const char *p, int64_t n) {
    if (n <= 0) return;
    if (b->len + n > b->cap) {
        int64_t nc = b->cap ? b->cap * 2 : 64;
        while (nc < b->len + n) nc *= 2;
        char *nd = realloc(b->d, (size_t)nc);
        if (!nd) mvx_fatal("out of memory in dynamic-array operation");
        b->d = nd;
        b->cap = nc;
    }
    memcpy(b->d + b->len, p, (size_t)n);
    b->len += n;
}

static void bch(dbuf *b, char c) { bput(b, &c, 1); }

static void bmarks(dbuf *b, char c, int64_t n) {
    while (n-- > 0) bch(b, c);
}

enum { OP_REPL, OP_INS, OP_DEL };

/* Apply op at the nesting level named by idx[0..nlev-1]; marks runs
   parallel {AM, VM, SM}.  Non-final levels navigate (padding with marks
   past the end); the final level edits.  idx < 0 appends at that level. */
static void modify(dbuf *out, span s, const char *marks,
                   const int64_t *idx, int nlev, span val, int op) {
    char mark = marks[0];
    int64_t i = idx[0];
    int64_t cnt = span_count(s, mark);

    if (nlev == 1) {
        if (op == OP_DEL) {
            if (cnt == 0 || i < 1 || i > cnt) {
                bput(out, s.p, s.len);
                return;
            }
            span e = nth_span(s, mark, i);
            if (i == cnt) {
                int64_t plen = e.p - s.p;
                if (plen > 0) plen -= 1;            /* preceding mark */
                bput(out, s.p, plen);
            } else {
                bput(out, s.p, e.p - s.p);
                const char *rest = e.p + e.len + 1; /* following mark */
                bput(out, rest, s.p + s.len - rest);
            }
            return;
        }
        if (i < 0) {                                /* append */
            bput(out, s.p, s.len);
            if (cnt > 0) bch(out, mark);
            bput(out, val.p, val.len);
            return;
        }
        if (i == 0) i = 1;
        if (i > cnt) {                              /* pad then place */
            bput(out, s.p, s.len);
            bmarks(out, mark, cnt ? i - cnt : i - 1);
            bput(out, val.p, val.len);
            return;
        }
        span e = nth_span(s, mark, i);
        if (op == OP_REPL) {
            bput(out, s.p, e.p - s.p);
            bput(out, val.p, val.len);
            bput(out, e.p + e.len, s.p + s.len - (e.p + e.len));
        } else {                                    /* OP_INS */
            bput(out, s.p, e.p - s.p);
            bput(out, val.p, val.len);
            bch(out, mark);
            bput(out, e.p, s.p + s.len - e.p);
        }
        return;
    }

    if (i < 0) {
        bput(out, s.p, s.len);
        if (cnt > 0) bch(out, mark);
        modify(out, (span){s.p, 0}, marks + 1, idx + 1, nlev - 1, val, op);
        return;
    }
    if (i == 0) i = 1;
    if (i > cnt) {
        bput(out, s.p, s.len);
        bmarks(out, mark, cnt ? i - cnt : i - 1);
        modify(out, (span){s.p, 0}, marks + 1, idx + 1, nlev - 1, val, op);
        return;
    }
    span e = nth_span(s, mark, i);
    bput(out, s.p, e.p - s.p);
    modify(out, e, marks + 1, idx + 1, nlev - 1, val, op);
    bput(out, e.p + e.len, s.p + s.len - (e.p + e.len));
}

static void run_op(mv_value *dst, const mv_value *src, int64_t a,
                   int64_t v, int64_t s, const mv_value *val, int op) {
    char nb[40], vb[40];
    span sp = val_span(src, nb, sizeof nb);
    span vs = val ? val_span(val, vb, sizeof vb) : (span){"", 0};
    static const char marks[3] = {AM, VM, SM};
    int64_t idx[3];
    int n = 0;
    idx[n++] = a;
    if (v != 0 || s != 0) idx[n++] = v;
    if (s != 0) idx[n++] = s;
    dbuf out = {0, 0, 0};
    modify(&out, sp, marks, idx, n, vs, op);
    mv_set_str(dst, out.d ? out.d : "", out.len);
    free(out.d);
}

void mv_replace_fn(mv_value *dst, const mv_value *src, int64_t a,
                   int64_t v, int64_t s, const mv_value *val) {
    run_op(dst, src, a, v, s, val, OP_REPL);
}

void mv_insert_fn(mv_value *dst, const mv_value *src, int64_t a,
                  int64_t v, int64_t s, const mv_value *val) {
    run_op(dst, src, a, v, s, val, OP_INS);
}

void mv_delete_fn(mv_value *dst, const mv_value *src, int64_t a,
                  int64_t v, int64_t s) {
    run_op(dst, src, a, v, s, NULL, OP_DEL);
}

/* --------------------------------------------------------------- LOCATE */

static int num_parse(span s, double *out) {
    char tmp[64];
    if (s.len == 0 || s.len >= (int64_t)sizeof tmp) return 0;
    memcpy(tmp, s.p, (size_t)s.len);
    tmp[s.len] = '\0';
    char *end = NULL;
    double d = strtod(tmp, &end);
    if (end == tmp + s.len) { *out = d; return 1; }
    return 0;
}

static int cmp_left(span a, span b) {
    int64_t min = a.len < b.len ? a.len : b.len;
    int c = memcmp(a.p, b.p, (size_t)min);
    if (c) return c;
    return (a.len > b.len) - (a.len < b.len);
}

static int cmp_right(span a, span b) {
    double x, y;
    if (num_parse(a, &x) && num_parse(b, &y)) return (x > y) - (x < y);
    if (a.len != b.len) return a.len < b.len ? -1 : 1;
    return memcmp(a.p, b.p, (size_t)a.len);
}

int64_t mv_locate_fn(const mv_value *item, const mv_value *src, int64_t a,
                     int64_t v, const mv_value *order, int64_t *pos) {
    char nb[40], ib[40], ob[40];
    span sp = val_span(src, nb, sizeof nb);
    char mark = AM;
    if (a > 0) { sp = nth_span(sp, AM, a); mark = VM; }
    if (a > 0 && v > 0) { sp = nth_span(sp, VM, v); mark = SM; }
    span it = val_span(item, ib, sizeof ib);

    char dir = 0, just = 'L';
    if (order) {
        span os = val_span(order, ob, sizeof ob);
        if (os.len >= 1) dir = (char)toupper((unsigned char)os.p[0]);
        if (os.len >= 2) just = (char)toupper((unsigned char)os.p[1]);
        if (dir != 'A' && dir != 'D') dir = 0;
    }

    int64_t n = span_count(sp, mark);
    const char *q = sp.p, *end = sp.p + sp.len;
    for (int64_t k = 1; k <= n; k++) {
        const char *m = memchr(q, mark, (size_t)(end - q));
        span e = {q, (m ? m : end) - q};
        if (e.len == it.len && memcmp(e.p, it.p, (size_t)e.len) == 0) {
            *pos = k;
            return 1;
        }
        if (dir) {
            int c = (just == 'R') ? cmp_right(it, e) : cmp_left(it, e);
            if ((dir == 'A' && c < 0) || (dir == 'D' && c > 0)) {
                *pos = k;                           /* insertion point */
                return 0;
            }
        }
        q = m ? m + 1 : end;
    }
    *pos = n + 1;
    return 0;
}

/* -------------------------------------------------- LEN / COUNT / DCOUNT */

int64_t mv_len_fn(const mv_value *v) {
    char nb[40];
    span s = val_span(v, nb, sizeof nb);
    return s.len;
}

int64_t mv_count_fn(const mv_value *src, const mv_value *what) {
    char nb[40], wb[40];
    span s = val_span(src, nb, sizeof nb);
    span w = val_span(what, wb, sizeof wb);
    if (w.len == 0 || s.len < w.len) return 0;
    int64_t n = 0;
    const char *p = s.p, *end = s.p + s.len;
    while (p + w.len <= end) {
        const char *m = memmem(p, (size_t)(end - p), w.p, (size_t)w.len);
        if (!m) break;
        n++;
        p = m + w.len;                              /* non-overlapping */
    }
    return n;
}

int64_t mv_dcount_fn(const mv_value *src, const mv_value *delim) {
    char nb[40];
    span s = val_span(src, nb, sizeof nb);
    if (s.len == 0) return 0;
    return mv_count_fn(src, delim) + 1;
}
