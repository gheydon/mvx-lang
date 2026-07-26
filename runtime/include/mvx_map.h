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

/* The single mapper: the dictionary/association projection and typing engine.
 *
 * A record is an @AM/@VM MultiValue string; a *mapping* says how its attributes
 * become structured fields — single-valued attributes as scalar columns/keys,
 * and associations as groups of multivalued members that decompose into rows /
 * an array of objects (one per value position).  This pure engine (no file or
 * storage-driver coupling) is shared by the SQL relational mapping (#18), the
 * JSON codec (#24), and any future consumer, so the projection lives once.
 *
 * The mapping is carried as a %MAP%-format string: @AM-separated fields, each
 * "name <VM> attr <VM> conv <VM> type <VM> assoc".  type is one of
 * TEXT/NUMERIC/DATE/TIME; assoc empty = a single-valued parent, else the name
 * of the association the field belongs to.
 */
#ifndef MVX_MAP_H
#define MVX_MAP_H

#include "mvx_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAP_MAXF 64
#define MAP_MAXA 16

/* A parsed mapping, cached per open file (loaded/ensured/native are used by the
   storage mirror; JSON uses only the parsed fields). */
typedef struct mapmeta {
    int loaded;
    int ensured;                        /* schema materialised this session */
    int native;                         /* %MAPMODE% = native: strict writes */
    int nf;
    char *buf;                          /* owns the parsed field strings */
    char *names[MAP_MAXF], *convs[MAP_MAXF], *types[MAP_MAXF];
    char *assocs[MAP_MAXF];
    int64_t anos[MAP_MAXF];
} mapmeta;

/* Parse a %MAP% / spec string into m, which owns the text (free m->buf). */
void map_parse(const char *sp, int64_t slen, mapmeta *m);

/* Reduce a display value to a plain number; 0 length (out="") when no digits. */
int64_t map_num(const char *in, int64_t len, char *out, size_t cap);

/* Extract attribute `ano` value `seq` (seq 0 = whole attribute), OCONV by
   `conv` if set, and coerce to `type`, into `dst`.  Returns the cell length, 0
   for empty, or -1 when a non-empty value does not fit the type.  av/ov/code are
   caller-provided scratch mv_values. */
int64_t map_cell(mvx_ctx *ctx, const mv_value *rec, int64_t ano, int64_t seq,
                 const char *conv, const char *type, mv_value *av, mv_value *ov,
                 mv_value *code, char *dst, size_t cap);

/* Inverse of map_cell: a stored column string (cv/cl; NULL/empty -> empty) back
   to the attribute's internal value in dst.  tmp/code are scratch. */
void map_uncell(mvx_ctx *ctx, const char *type, const char *conv,
                const char *cv, int64_t cl, mv_value *dst, mv_value *tmp,
                mv_value *code);

/* Number of values in an attribute (0 if empty); av is scratch. */
int map_vcount(const mv_value *rec, int64_t ano, mv_value *av);

/* Group the mapping's association members: fills an[]/am[][]/anm[] (member field
   indices per association) and returns the association count. */
int map_group_assoc(mapmeta *m, char *an[MAP_MAXA], int am[MAP_MAXA][MAP_MAXF],
                    int anm[MAP_MAXA]);

/* True when attribute `ano` is byte-identical in two records; ta/tb scratch. */
int map_attr_equal(const mv_value *a, const mv_value *b, int64_t ano,
                   mv_value *ta, mv_value *tb);

/* The abstract type a conversion code implies: MD/MR/ML -> NUMERIC, MT -> TIME,
   D... -> DATE, else TEXT.  The one derivation, shared by MAPSPEC and MAPFIELD. */
const char *map_type_from_conv(const char *conv);

/* Build one mapping field "name<VM>attr<VM>conv<VM>type<VM>assoc" into dst (the
   MAPFIELD helper).  When `type` is empty it is derived from `conv`.  Callers
   append fields with SPEC<-1> = MAPFIELD(...) to assemble a full mapping. */
void mvx_map_field(mv_value *dst, const mv_value *name, const mv_value *attr,
                  const mv_value *conv, const mv_value *type,
                  const mv_value *assoc);

#ifdef __cplusplus
}
#endif
#endif /* MVX_MAP_H */
