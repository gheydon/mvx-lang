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

/* Runtime CALL resolution — the jBASE catalog model: subroutines
 * compile into shared libraries, and CALL binds at call time.
 *
 * Resolution order for mvx_sub_<NAME>:
 *   1. symbols already in the process (subroutines compiled into the
 *      program itself, or in libraries loaded earlier);
 *   2. cataloged subroutine libraries, loaded on first miss from the
 *      account's LIB/, each linked package's LIB/ (PACKAGES record),
 *      and the system account's LIB/.
 *
 * CALL @VAR routes here too, with the name taken from the variable —
 * which is what makes dispatch tables (and command frameworks)
 * possible.
 */
#include "mvx_runtime.h"
#include "mvx_ext.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef void (*mvx_subfn)(mvx_ctx *, int32_t, mv_value **);

/* Cataloged subroutine (mvx_sub_<NAME>) or, failing that, an extension-package
   function.  Library loading (account LIB/, linked packages, system account) is
   shared with the extension registry — mvx_ext_load_libs() dlopen's every
   package lib RTLD_GLOBAL, so both mvx_sub_ symbols and mvx_ext tables land in
   one pass. */
static mvx_subfn find_sub(const char *name) {
    char sym[300];
    snprintf(sym, sizeof sym, "mvx_sub_%s", name);
    void *p = dlsym(RTLD_DEFAULT, sym);
    if (!p) {
        mvx_ext_load_libs();
        p = dlsym(RTLD_DEFAULT, sym);
    }
    return (mvx_subfn)p;
}

void mvx_call(mvx_ctx *ctx, const char *name, int32_t argc,
              mv_value **argv) {
    mvx_subfn f = find_sub(name);
    if (f) { f(ctx, argc, argv); return; }
    /* Not a cataloged subroutine — try an extension-package function (a native
       function called as a statement; its result, if any, is discarded). */
    if (mvx_ext_has(name)) {
        mv_value scratch;
        mv_init(&scratch);
        mvx_ext_invoke(ctx, name, &scratch, argc, argv);
        mv_clear(&scratch);
        return;
    }
    mvx_fatal("CALL %s: subroutine is not cataloged (searched the "
              "program, LIB/, linked packages, and the system "
              "account)", name);
}

void mvx_call_var(mvx_ctx *ctx, const mv_value *namev, int32_t argc,
                  mv_value **argv) {
    char nb[40];
    const char *p;
    int64_t n = mv_val_chars(namev, nb, sizeof nb, &p);
    if (n <= 0 || n >= 256)
        mvx_fatal("CALL @: variable does not hold a subroutine name");
    char name[256];
    memcpy(name, p, (size_t)n);
    name[n] = '\0';
    mvx_call(ctx, name, argc, argv);
}
