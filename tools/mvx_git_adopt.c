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

/* mvx-git-adopt — adopt a *plain-git* checkout of an MVX account into a live
 * native account.  Use it after checking out an account (or the mvx-lang system
 * account and packages) with plain git — e.g. CI's actions/checkout:
 *
 *   mvx-git-adopt <account>
 *
 * It reads the checked-out directory form (the open account format, or the
 * platform's own legible form) and builds the live hash-file account in place:
 * a dictionary's %FILE% (DIR/hash, or a native FILE<VM>type) names each file's
 * backend, and `.mv-account` becomes the native `.mvx`.
 *
 * `mvx-git` never needs this — it materialises the account directly from the git
 * objects.  There is no reverse (export) direction: committing an MV account to
 * git always goes through the record-git engine (mvx-git / udt-git).
 */

#include "mvx_runtime.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            fprintf(stderr, "usage: mvx-git-adopt <account>\n");
            return 2;
        }
        dir = argv[i];
    }
    if (!dir) {
        fprintf(stderr, "usage: mvx-git-adopt <account>\n");
        return 2;
    }
    setenv("MVXACCOUNT", dir, 1);
    mvx_ctx *ctx = mvx_ctx_create();
    int rc = mvx_acct_import(ctx);
    mvx_ctx_destroy(ctx);
    return rc;
}
