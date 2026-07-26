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

/* The single mapper — the pure dictionary/association projection and typing
   engine shared by storage (#18) and JSON (#24).  See mvx_map.h. */

#include "mvx_map.h"

#include <stdlib.h>
#include <string.h>

int64_t map_num(const char *in, int64_t len, char *out, size_t cap) {
    int neg = 0;
    for (int64_t i = 0; i < len; i++)
        if (in[i] == '-') neg = 1;
    size_t o = 0;
    int dot = 0, digits = 0;
    if (neg && o < cap - 1) out[o++] = '-';
    for (int64_t i = 0; i < len && o < cap - 1; i++) {
        char ch = in[i];
        if (ch >= '0' && ch <= '9') { out[o++] = ch; digits = 1; }
        else if (ch == '.' && !dot) { out[o++] = '.'; dot = 1; }
    }
    if (!digits) { out[0] = '\0'; return 0; }
    out[o] = '\0';
    return (int64_t)o;
}

int64_t map_cell(mvx_ctx *ctx, const mv_value *rec, int64_t ano,
                 int64_t seq, const char *conv, const char *type,
                 mv_value *av, mv_value *ov, mv_value *code,
                 char *dst, size_t cap) {
    mv_extract_fn(av, rec, ano, seq, 0);
    /* DATE/TIME columns take the stored internal value (day/second count)
       straight to ISO-8601 — the dict display conversion is locale-shaped
       and would be ambiguous to the backend.  An empty cell is NULL; a
       non-numeric internal is a type mismatch. */
    if (strcmp(type, "DATE") == 0 || strcmp(type, "TIME") == 0) {
        char ib[40];
        const char *ip;
        int64_t il = mv_val_chars(av, ib, sizeof ib, &ip);
        if (il == 0) { dst[0] = '\0'; return 0; }
        int64_t n = type[0] == 'D' ? mvx_iso_date_str(ip, il, dst, cap)
                                   : mvx_iso_time_str(ip, il, dst, cap);
        return n > 0 ? n : -1;
    }
    const mv_value *src = av;
    if (conv[0]) {
        mv_set_str(code, conv, (int64_t)strlen(conv));
        mv_oconv(ctx, ov, av, code);
        src = ov;
    }
    char tb[40];
    const char *tp;
    int64_t tl = mv_val_chars(src, tb, sizeof tb, &tp);
    if (strcmp(type, "NUMERIC") == 0) {
        int64_t n = map_num(tp, tl, dst, cap);
        return (n == 0 && tl > 0) ? -1 : n;       /* non-numeric text */
    }
    if (tl >= (int64_t)cap) tl = (int64_t)cap - 1;
    memcpy(dst, tp, (size_t)tl);
    dst[tl] = '\0';
    return tl;
}

int map_vcount(const mv_value *rec, int64_t ano, mv_value *av) {
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

void map_parse(const char *sp, int64_t slen, mapmeta *m) {
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

int map_attr_equal(const mv_value *a, const mv_value *b, int64_t ano,
                   mv_value *ta, mv_value *tb) {
    mv_extract_fn(ta, a, ano, 0, 0);
    mv_extract_fn(tb, b, ano, 0, 0);
    char ba[64], bb[64];
    const char *pa, *pb;
    int64_t la = mv_val_chars(ta, ba, sizeof ba, &pa);
    int64_t lb = mv_val_chars(tb, bb, sizeof bb, &pb);
    return la == lb && (la == 0 || memcmp(pa, pb, (size_t)la) == 0);
}

int map_group_assoc(mapmeta *m, char *an[MAP_MAXA],
                    int am[MAP_MAXA][MAP_MAXF], int anm[MAP_MAXA]) {
    int na = 0;
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
    return na;
}

void map_uncell(mvx_ctx *ctx, const char *type, const char *conv,
                const char *cv, int64_t cl, mv_value *dst,
                mv_value *tmp, mv_value *code) {
    if (cv == NULL || cl == 0) { mv_set_str(dst, "", 0); return; }
    if (strcmp(type, "DATE") == 0) {
        char ib[32];
        int64_t n = mvx_iso_date_intern(cv, cl, ib, sizeof ib);
        mv_set_str(dst, ib, n);
        return;
    }
    if (strcmp(type, "TIME") == 0) {
        char ib[32];
        int64_t n = mvx_iso_time_intern(cv, cl, ib, sizeof ib);
        mv_set_str(dst, ib, n);
        return;
    }
    if (conv[0]) {
        mv_set_str(tmp, cv, cl);
        mv_set_str(code, conv, (int64_t)strlen(conv));
        mv_iconv(ctx, dst, tmp, code);
        return;
    }
    mv_set_str(dst, cv, cl);
}

const char *map_type_from_conv(const char *conv) {
    if (!conv || !conv[0]) return "TEXT";
    if (conv[0] == 'D') return "DATE";                 /* D, D2/, D4-, ... */
    if (conv[0] == 'M') {
        char c = conv[1];
        if (c == 'D' || c == 'R' || c == 'L') return "NUMERIC";  /* masked dec */
        if (c == 'T') return "TIME";                             /* masked time */
    }
    return "TEXT";
}

void mvx_map_field(mv_value *dst, const mv_value *name, const mv_value *attr,
                  const mv_value *conv, const mv_value *type,
                  const mv_value *assoc) {
    char nb[128], ab[40], cb[64], tb[16], sb[128];
    const char *np = "", *ap = "", *cp = "", *tp = "", *sp = "";
    /* trailing args are optional at the call site (MAPFIELD 2..5 args) — a NULL
       is treated as empty, and an empty type is derived from the conversion. */
    int64_t nl = name  ? mv_val_chars(name,  nb, sizeof nb, &np) : 0;
    int64_t al = attr  ? mv_val_chars(attr,  ab, sizeof ab, &ap) : 0;
    int64_t cl = conv  ? mv_val_chars(conv,  cb, sizeof cb, &cp) : 0;
    int64_t tl = type  ? mv_val_chars(type,  tb, sizeof tb, &tp) : 0;
    int64_t sl = assoc ? mv_val_chars(assoc, sb, sizeof sb, &sp) : 0;

    /* an empty type is derived from the conversion code */
    char cz[64];
    if (cl > 0) { if (cl >= (int64_t)sizeof cz) cl = sizeof cz - 1;
                  memcpy(cz, cp, (size_t)cl); cz[cl] = '\0'; }
    else cz[0] = '\0';
    const char *dtype = tp;
    int64_t dtlen = tl;
    if (tl == 0) { dtype = map_type_from_conv(cz); dtlen = (int64_t)strlen(dtype); }

    size_t need = (size_t)(nl + al + cl + dtlen + sl) + 8;
    char *buf = malloc(need);
    if (!buf) { mv_set_str(dst, "", 0); return; }
    size_t o = 0;
    memcpy(buf + o, np, (size_t)nl); o += (size_t)nl; buf[o++] = (char)0xFD;
    memcpy(buf + o, ap, (size_t)al); o += (size_t)al; buf[o++] = (char)0xFD;
    memcpy(buf + o, cp, (size_t)cl); o += (size_t)cl; buf[o++] = (char)0xFD;
    memcpy(buf + o, dtype, (size_t)dtlen); o += (size_t)dtlen; buf[o++] = (char)0xFD;
    memcpy(buf + o, sp, (size_t)sl); o += (size_t)sl;
    mv_set_str(dst, buf, (int64_t)o);
    free(buf);
}
