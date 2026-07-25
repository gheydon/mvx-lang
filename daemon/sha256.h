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

/* Minimal, self-contained SHA-256 — shared by mvx-lmdbd (verify a token)
 * and mvx-lmdbd-admin (hash a token on create).  No external
 * dependencies, so both stay standalone and Pick-agnostic. */
#ifndef MVXD_SHA256_H
#define MVXD_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buf[64];
    size_t buflen;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t len);
void sha256_final(sha256_ctx *c, uint8_t out[32]);

/* Hex(SHA-256(salt || token)) into out (65 bytes incl. NUL). */
void sha256_salted_hex(const char *salt, const char *token, char out[65]);

#endif /* MVXD_SHA256_H */
