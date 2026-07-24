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

/* String intrinsics: CHAR, SEQ, STR, SPACE, TRIM, FIELD, INDEX, NUM. */
#include "mvx_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *p; int64_t len; } span;

/* Same rendering as the dynamic-array module. */
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

void mv_char_fn(mv_value *dst, int64_t code) {
    char c = (char)(code & 0xFF);
    mv_set_str(dst, &c, 1);
}

int64_t mv_seq_fn(const mv_value *v) {
    char nb[40];
    span s = val_span(v, nb, sizeof nb);
    return s.len ? (unsigned char)s.p[0] : 0;
}

void mv_space_fn(mv_value *dst, int64_t n) {
    if (n <= 0) { mv_set_str(dst, "", 0); return; }
    char *buf = malloc((size_t)n);
    if (!buf) mvx_fatal("out of memory in SPACE(%lld)", (long long)n);
    memset(buf, ' ', (size_t)n);
    mv_set_str(dst, buf, n);
    free(buf);
}

void mv_str_fn(mv_value *dst, const mv_value *src, int64_t n) {
    char nb[40];
    span s = val_span(src, nb, sizeof nb);
    if (n <= 0 || s.len == 0) { mv_set_str(dst, "", 0); return; }
    int64_t total = s.len * n;
    char *buf = malloc((size_t)total);
    if (!buf) mvx_fatal("out of memory in STR of %lld bytes",
                        (long long)total);
    for (int64_t k = 0; k < n; k++)
        memcpy(buf + k * s.len, s.p, (size_t)s.len);
    mv_set_str(dst, buf, total);
    free(buf);
}

/* Classic TRIM: strip leading/trailing blanks, squeeze runs of blanks
   (spaces and tabs) to a single space. */
void mv_trim_fn(mv_value *dst, const mv_value *src) {
    char nb[40];
    span s = val_span(src, nb, sizeof nb);
    char *buf = malloc(s.len ? (size_t)s.len : 1);
    if (!buf) mvx_fatal("out of memory in TRIM");
    int64_t out = 0;
    int pending = 0;
    for (int64_t i = 0; i < s.len; i++) {
        char c = s.p[i];
        if (c == ' ' || c == '\t') {
            if (out > 0) pending = 1;
        } else {
            if (pending) { buf[out++] = ' '; pending = 0; }
            buf[out++] = c;
        }
    }
    mv_set_str(dst, buf, out);
    free(buf);
}

void mv_field_fn(mv_value *dst, const mv_value *src, const mv_value *delim,
                 int64_t n, int64_t cnt) {
    char nb[40], db[40];
    span s = val_span(src, nb, sizeof nb);
    span d = val_span(delim, db, sizeof db);
    if (n < 1) n = 1;
    if (cnt < 1) cnt = 1;
    if (d.len == 0) {
        if (n == 1) mv_set_str(dst, s.p, s.len);
        else        mv_set_str(dst, "", 0);
        return;
    }
    const char *end = s.p + s.len;
    const char *q = s.p;
    for (int64_t k = 1; k < n; k++) {
        const char *m = memmem(q, (size_t)(end - q), d.p, (size_t)d.len);
        if (!m) { mv_set_str(dst, "", 0); return; }
        q = m + d.len;
    }
    const char *stop = q;
    for (int64_t k = 0; k < cnt; k++) {
        const char *m = memmem(stop, (size_t)(end - stop), d.p,
                               (size_t)d.len);
        if (!m) { stop = end; break; }
        stop = (k == cnt - 1) ? m : m + d.len;
    }
    mv_set_str(dst, q, stop - q);
}

int64_t mv_index_fn(const mv_value *src, const mv_value *sub, int64_t occ) {
    char nb[40], sb[40];
    span s = val_span(src, nb, sizeof nb);
    span w = val_span(sub, sb, sizeof sb);
    if (w.len == 0 || occ < 1) return 0;
    const char *end = s.p + s.len;
    const char *q = s.p;
    for (int64_t k = 0; k < occ; k++) {
        if (q + w.len > end) return 0;
        const char *m = memmem(q, (size_t)(end - q), w.p, (size_t)w.len);
        if (!m) return 0;
        if (k == occ - 1) return (m - s.p) + 1;
        q = m + 1;                                  /* occurrences overlap */
    }
    return 0;
}

/* CHANGE(s, old, new): replace every non-overlapping occurrence of old
   with new (classic MV; empty old returns s unchanged). */
void mv_change_fn(mv_value *dst, const mv_value *src, const mv_value *oldv,
                  const mv_value *newv) {
    char sb[40], ob[40], nb[40];
    span s = val_span(src, sb, sizeof sb);
    span o = val_span(oldv, ob, sizeof ob);
    span n = val_span(newv, nb, sizeof nb);
    if (o.len == 0 || s.len < o.len) {
        mv_set_str(dst, s.p, s.len);
        return;
    }
    char *buf = NULL;
    int64_t len = 0, cap = 0;
    const char *p = s.p, *end = s.p + s.len;
    while (p < end) {
        const char *m = (p + o.len <= end)
            ? memmem(p, (size_t)(end - p), o.p, (size_t)o.len)
            : NULL;
        int64_t chunk = m ? (m - p) : (end - p);
        int64_t add = chunk + (m ? n.len : 0);
        if (len + add > cap) {
            cap = cap ? cap * 2 : 64;
            while (cap < len + add) cap *= 2;
            char *nbuf = realloc(buf, (size_t)cap);
            if (!nbuf) mvx_fatal("out of memory in CHANGE");
            buf = nbuf;
        }
        memcpy(buf + len, p, (size_t)chunk);
        len += chunk;
        if (m) {
            memcpy(buf + len, n.p, (size_t)n.len);
            len += n.len;
            p = m + o.len;
        } else {
            p = end;
        }
    }
    mv_set_str(dst, buf ? buf : "", len);
    free(buf);
}

/* X[start,len] — MV semantics: 1-based; start past the end yields "";
   len clipped to what remains; start < 1 clamps to 1. */
void mv_substr(mv_value *dst, const mv_value *src, int64_t start,
               int64_t len) {
    char nb[40];
    span s = val_span(src, nb, sizeof nb);
    if (start < 1) start = 1;
    if (start > s.len || len <= 0) {
        mv_set_str(dst, "", 0);
        return;
    }
    int64_t avail = s.len - (start - 1);
    if (len > avail) len = avail;
    mv_set_str(dst, s.p + start - 1, len);
}

int64_t mv_num_fn(const mv_value *v) {
    switch (v->tag) {
    case MV_INT:
    case MV_DBL:
        return 1;
    case MV_STR: {
        if (v->s->len == 0) return 1;               /* classic: "" is numeric */
        char *end = NULL;
        strtod(v->s->data, &end);
        return end == v->s->data + v->s->len;
    }
    default:
        return 1;                                   /* unassigned acts as 0 */
    }
}
