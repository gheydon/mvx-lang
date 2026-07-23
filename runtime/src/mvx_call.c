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

#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#define LIB_SUFFIX ".dylib"
#else
#define LIB_SUFFIX ".so"
#endif

#ifndef MVX_SYSTEM_DIR
#define MVX_SYSTEM_DIR "."
#endif

typedef void (*mvx_subfn)(mvx_ctx *, int32_t, mv_value **);

static int g_libs_loaded;

static void load_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    size_t sl = strlen(LIB_SUFFIX);
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n <= sl || strcmp(e->d_name + n - sl, LIB_SUFFIX) != 0)
            continue;
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        dlopen(path, RTLD_NOW | RTLD_GLOBAL);   /* failure: sym stays absent */
    }
    closedir(d);
}

static void load_libs(void) {
    if (g_libs_loaded) return;
    g_libs_loaded = 1;

    load_dir("LIB");                            /* account catalog */

    FILE *fp = fopen("PACKAGES", "r");          /* linked packages */
    if (fp) {
        char ln[1024];
        while (fgets(ln, sizeof ln, fp)) {
            size_t n = strlen(ln);
            while (n && (ln[n - 1] == '\n' || ln[n - 1] == '\r' ||
                         ln[n - 1] == ' '))
                ln[--n] = '\0';
            if (!n) continue;
            char lib[1152];
            snprintf(lib, sizeof lib, "%s/LIB", ln);
            load_dir(lib);
        }
        fclose(fp);
    }

    const char *sys = getenv("MVXSYSTEM");
    if (!sys || !sys[0]) sys = MVX_SYSTEM_DIR;
    char syslib[4096];
    snprintf(syslib, sizeof syslib, "%s/LIB", sys);
    load_dir(syslib);
}

static mvx_subfn find_sub(const char *name) {
    char sym[300];
    snprintf(sym, sizeof sym, "mvx_sub_%s", name);
    void *p = dlsym(RTLD_DEFAULT, sym);
    if (!p) {
        load_libs();
        p = dlsym(RTLD_DEFAULT, sym);
    }
    return (mvx_subfn)p;
}

void mvx_call(mvx_ctx *ctx, const char *name, int32_t argc,
              mv_value **argv) {
    mvx_subfn f = find_sub(name);
    if (!f)
        mvx_fatal("CALL %s: subroutine is not cataloged (searched the "
                  "program, LIB/, linked packages, and the system "
                  "account)", name);
    f(ctx, argc, argv);
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
