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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *p; int64_t len; } span;
static span val_span(const mv_value *v, char *buf, size_t cap);

/* ---- MATCHES / MATCHFIELD pattern matching ------------------------------
   A pattern is a run of tokens: `nA`/`nN`/`nX` (exactly n alphabetic /
   numeric / any), `0A`/`0N`/`0X` (zero or more of that class), a quoted
   literal ('...' or "..."), or any other character taken literally.  The
   subject must be consumed whole.  Value marks (@VM) in the pattern give
   alternatives — a match against any one succeeds. */
#define PAT_MAX 64
typedef struct { int count; char cls; const char *lit; int litlen; } ptok;

static int cls_ok(unsigned char c, char cls) {
    switch (cls) {
    case 'N': return c >= '0' && c <= '9';
    case 'A': return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    default:  return 1;                              /* 'X': any */
    }
}

static int pat_tok(const char *p, int plen, ptok *t) {
    int nt = 0, i = 0;
    while (i < plen && nt < PAT_MAX) {
        char c = p[i];
        if (c == '"' || c == '\'') {                /* quoted literal */
            int j = i + 1;
            while (j < plen && p[j] != c) j++;
            t[nt].cls = 0; t[nt].lit = p + i + 1; t[nt].litlen = j - (i + 1);
            nt++; i = (j < plen) ? j + 1 : j;
            continue;
        }
        if (c >= '0' && c <= '9') {                 /* n<class> ? */
            int j = i, n = 0;
            while (j < plen && p[j] >= '0' && p[j] <= '9')
                n = n * 10 + (p[j++] - '0');
            char k = (j < plen) ? (char)toupper((unsigned char)p[j]) : 0;
            if (k == 'A' || k == 'N' || k == 'X') {
                t[nt].count = n; t[nt].cls = k; t[nt].lit = NULL;
                nt++; i = j + 1;
                continue;
            }
        }
        t[nt].cls = 0; t[nt].lit = p + i; t[nt].litlen = 1;  /* char literal */
        nt++; i++;
    }
    return nt;
}

/* Match subject [s,se) against tokens; record each token's span in bnd
   (may be NULL) on the winning path. */
static int pat_match(const char *s, const char *se, const ptok *t, int nt,
                     int ti, span *bnd) {
    if (ti == nt) return s == se;
    const ptok *k = &t[ti];
    if (k->cls == 0) {                              /* literal */
        if (se - s >= k->litlen && memcmp(s, k->lit, (size_t)k->litlen) == 0) {
            if (bnd) { bnd[ti].p = s; bnd[ti].len = k->litlen; }
            return pat_match(s + k->litlen, se, t, nt, ti + 1, bnd);
        }
        return 0;
    }
    if (k->count > 0) {                             /* exactly n */
        if (se - s < k->count) return 0;
        for (int j = 0; j < k->count; j++)
            if (!cls_ok((unsigned char)s[j], k->cls)) return 0;
        if (bnd) { bnd[ti].p = s; bnd[ti].len = k->count; }
        return pat_match(s + k->count, se, t, nt, ti + 1, bnd);
    }
    const char *m = s;                              /* 0: zero or more */
    while (m < se && cls_ok((unsigned char)*m, k->cls)) m++;
    for (const char *q = m; q >= s; q--) {          /* greedy, backtrack */
        if (bnd) { bnd[ti].p = s; bnd[ti].len = q - s; }
        if (pat_match(q, se, t, nt, ti + 1, bnd)) return 1;
    }
    return 0;
}

/* Match, trying each @VM-separated alternative.  Fills *win/*wnt with the
   winning alternative's tokens (for MATCHFIELD) when winner != NULL. */
static int pat_any(const char *s, int64_t slen, const char *p, int64_t plen,
                   ptok *win, int *wnt) {
    const char *pe = p + plen, *seg = p;
    for (const char *q = p;; q++) {
        if (q == pe || (unsigned char)*q == 0xFD) {
            ptok toks[PAT_MAX];
            int nt = pat_tok(seg, (int)(q - seg), toks);
            if (pat_match(s, s + slen, toks, nt, 0, NULL)) {
                if (win) { memcpy(win, toks, sizeof(ptok) * nt); *wnt = nt; }
                return 1;
            }
            if (q == pe) break;
            seg = q + 1;
        }
    }
    return 0;
}

int64_t mv_matches(const mv_value *subj, const mv_value *pat) {
    char sb[40], pb[40];
    span s = val_span(subj, sb, sizeof sb);
    span p = val_span(pat, pb, sizeof pb);
    return pat_any(s.p, s.len, p.p, p.len, NULL, NULL) ? 1 : 0;
}

/* MATCHFIELD(str, pattern, n): the substring matched by the n-th pattern
   component, or "" if the string does not match. */
void mv_matchfield_fn(mv_value *dst, const mv_value *subj, const mv_value *pat,
                      int64_t n) {
    char sb[40], pb[40];
    span s = val_span(subj, sb, sizeof sb);
    span p = val_span(pat, pb, sizeof pb);
    ptok win[PAT_MAX];
    int wnt = 0;
    if (n < 1 || !pat_any(s.p, s.len, p.p, p.len, win, &wnt) || n > wnt) {
        mv_set_str(dst, "", 0);
        return;
    }
    span bnd[PAT_MAX];
    if (!pat_match(s.p, s.p + s.len, win, wnt, 0, bnd)) {
        mv_set_str(dst, "", 0);
        return;
    }
    mv_set_str(dst, bnd[n - 1].p, bnd[n - 1].len);
}

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

/* TRIMB — remove trailing spaces/tabs only. */
void mv_trimb_fn(mv_value *dst, const mv_value *src) {
    char nb[40];
    span s = val_span(src, nb, sizeof nb);
    int64_t n = s.len;
    while (n > 0 && (s.p[n - 1] == ' ' || s.p[n - 1] == '\t')) n--;
    mv_set_str(dst, s.p, n);
}

/* TRIMF — remove leading spaces/tabs only. */
void mv_trimf_fn(mv_value *dst, const mv_value *src) {
    char nb[40];
    span s = val_span(src, nb, sizeof nb);
    int64_t i = 0;
    while (i < s.len && (s.p[i] == ' ' || s.p[i] == '\t')) i++;
    mv_set_str(dst, s.p + i, s.len - i);
}

/* CONVERT(from, to, str) — translate each character of str: a char found
 * at position i of `from` becomes to[i], or is deleted when `to` is
 * shorter; characters not in `from` pass through unchanged. */
void mv_convert_fn(mv_value *dst, const mv_value *fromv,
                   const mv_value *tov, const mv_value *src) {
    char fb[40], tb[40], sb[40];
    span from = val_span(fromv, fb, sizeof fb);
    span to = val_span(tov, tb, sizeof tb);
    span s = val_span(src, sb, sizeof sb);
    char *buf = malloc(s.len ? (size_t)s.len : 1);
    if (!buf) mvx_fatal("out of memory in CONVERT");
    int64_t out = 0;
    for (int64_t i = 0; i < s.len; i++) {
        char c = s.p[i];
        int64_t j = -1;
        for (int64_t k = 0; k < from.len; k++)
            if (from.p[k] == c) { j = k; break; }
        if (j < 0) buf[out++] = c;
        else if (j < to.len) buf[out++] = to.p[j];
        /* j >= to.len: the character is deleted */
    }
    mv_set_str(dst, buf, out);
    free(buf);
}

/* ALPHA — 1 when the string is non-empty and all letters, else 0. */
int64_t mv_alpha_fn(const mv_value *src) {
    char nb[40];
    span s = val_span(src, nb, sizeof nb);
    if (s.len == 0) return 0;
    for (int64_t i = 0; i < s.len; i++) {
        char c = s.p[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return 0;
    }
    return 1;
}

/* QUOTE/DQUOTE/SQUOTE — wrap the string in the quote character q. */
void mv_quote_fn(mv_value *dst, const mv_value *src, int64_t q) {
    char nb[40];
    span s = val_span(src, nb, sizeof nb);
    char *buf = malloc((size_t)s.len + 2);
    if (!buf) mvx_fatal("out of memory in QUOTE");
    buf[0] = (char)q;
    if (s.len) memcpy(buf + 1, s.p, (size_t)s.len);
    buf[s.len + 1] = (char)q;
    mv_set_str(dst, buf, s.len + 2);
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
