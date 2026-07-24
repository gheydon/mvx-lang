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

/* Program entry for compiled MVX main programs.  Kept out of libmvxrt.a
 * so that shared-library subroutine builds never pick up a main().
 * The driver links this object explicitly when producing an executable.
 */
#include "mvx_runtime.h"

int main(void) {
    mvx_ctx *ctx = mvx_ctx_create();
    mvx_main(ctx);
    mvx_ctx_destroy(ctx);
    return 0;
}
