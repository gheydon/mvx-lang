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

/* mvx-convert-acct — convert an account between the git directory form
 * and live hash files.  Use it after checking out an account (or the
 * mvx-lang system account and packages) with plain git, to turn the
 * directory form into a working hash-file account:
 *
 *   mvx-convert-acct <account>            directory form -> hash files
 *   mvx-convert-acct --export <account>   hash files -> directory form
 *
 * mvx-git runs this for you; you only need it for a plain git checkout.
 */

#include "mvx_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int do_export = 0;
    const char *dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--export")) do_export = 1;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: mvx-convert-acct [--export] <account>\n");
            return 2;
        } else dir = argv[i];
    }
    if (!dir) {
        fprintf(stderr, "usage: mvx-convert-acct [--export] <account>\n");
        return 2;
    }
    setenv("MVXACCOUNT", dir, 1);
    mvx_ctx *ctx = mvx_ctx_create();
    int rc = do_export ? mvx_acct_export(ctx) : mvx_acct_import(ctx);
    mvx_ctx_destroy(ctx);
    return rc;
}
