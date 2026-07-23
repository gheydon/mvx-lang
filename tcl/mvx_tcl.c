/* MVX classic TCL — the dispatch engine, and only the dispatch engine.
 *
 * This C program implements: the prompt, the builtin table, VOC lookup,
 * and spawning cataloged verb executables.  Verbs themselves are BASIC
 * programs (ARCHITECTURE.md 6.2); this surface is meant to freeze once
 * complete.
 *
 * Builtins (shell-internal by nature, cannot be verbs):
 *   OFF / QUIT / BYE   end the session
 *   ! <command>        raw passthrough to Unix
 *
 * NOTE: '!' currently spawns unconditionally.  The privilege gate
 * belongs in the runtime exec primitive (ARCHITECTURE.md 8.1) and will
 * move there when EXECUTE lands — a check that lives only here would be
 * decorative.
 *
 * Dispatch order: builtin table, then VOC, then not-found.
 * VOC verb record: attr 1 = "V", attr 2 = executable path relative to
 * the account directory.  The command sentence reaches the verb via
 * $MVX_SENTENCE (the SENTENCE() intrinsic).
 */
#include "mvx_runtime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MVX_SYSTEM_DIR
#define MVX_SYSTEM_DIR "."
#endif

static mvx_ctx *g_ctx;
static mv_value g_voc, g_sysvoc;
static int g_voc_state, g_sysvoc_state; /* 0 untried, 1 open, -1 absent */

static const char *system_dir(void) {
    const char *p = getenv("MVXSYSTEM");
    return p && p[0] ? p : MVX_SYSTEM_DIR;
}

static int voc_open(mv_value *voc, const char *spec) {
    mv_value s;
    mv_init(&s);
    mv_set_str(&s, spec, (int64_t)strlen(spec));
    mv_init(voc);
    int ok = mvx_open(g_ctx, NULL, &s, voc) ? 1 : -1;
    mv_clear(&s);
    return ok;
}

/* Read a V-record from the given VOC; path receives attribute 2. */
static int voc_read(mv_value *voc, const char *verb, char *path,
                    size_t cap) {
    mv_value id, rec, a1, a2;
    mv_init(&id); mv_init(&rec); mv_init(&a1); mv_init(&a2);
    mv_set_str(&id, verb, (int64_t)strlen(verb));
    int found = 0;
    if (mvx_read(g_ctx, &rec, voc, &id, 0)) {
        mv_extract_fn(&a1, &rec, 1, 0, 0);
        mv_extract_fn(&a2, &rec, 2, 0, 0);
        char nb[40];
        const char *p;
        int64_t n = mv_val_chars(&a1, nb, sizeof nb, &p);
        if (n >= 1 && (p[0] == 'V' || p[0] == 'v')) {
            n = mv_val_chars(&a2, nb, sizeof nb, &p);
            if (n > 0 && (size_t)n < cap) {
                memcpy(path, p, (size_t)n);
                path[n] = '\0';
                found = 1;
            }
        }
    }
    mv_clear(&id); mv_clear(&rec); mv_clear(&a1); mv_clear(&a2);
    return found;
}

/* Linked packages: the account's PACKAGES record (one path per line,
   maintained by LINK-PKG / UNLINK-PKG) names package directories whose
   VOCs join the resolution chain.  Reloaded when the file changes, so
   a LINK-PKG takes effect in the same session. */
#define MAX_PKGS 16
static char g_pkgs[MAX_PKGS][1024];
static mv_value g_pkgvoc[MAX_PKGS];
static int g_pkgvoc_state[MAX_PKGS];
static int g_npkgs;
static long long g_pkg_stamp = -1;

static void pkgs_reload(void) {
    struct stat sb;
    long long mt = 0;
    if (stat("PACKAGES", &sb) == 0) {
        /* Nanosecond stamp + size: whole-second mtime misses a LINK-PKG
           landing in the same second as the previous reload. */
#ifdef __APPLE__
        mt = (long long)sb.st_mtimespec.tv_sec * 1000000000LL +
             sb.st_mtimespec.tv_nsec + sb.st_size;
#else
        mt = (long long)sb.st_mtim.tv_sec * 1000000000LL +
             sb.st_mtim.tv_nsec + sb.st_size;
#endif
    }
    if (mt == g_pkg_stamp) return;
    g_pkg_stamp = mt;
    for (int i = 0; i < g_npkgs; i++)
        if (g_pkgvoc_state[i] > 0) mv_clear(&g_pkgvoc[i]);
    g_npkgs = 0;
    FILE *fp = fopen("PACKAGES", "r");
    if (!fp) return;
    char ln[1024];
    while (fgets(ln, sizeof ln, fp) && g_npkgs < MAX_PKGS) {
        size_t n = strlen(ln);
        while (n && (ln[n - 1] == '\n' || ln[n - 1] == '\r' ||
                     ln[n - 1] == ' '))
            ln[--n] = '\0';
        if (n == 0) continue;
        snprintf(g_pkgs[g_npkgs], sizeof g_pkgs[0], "%s", ln);
        g_pkgvoc_state[g_npkgs] = 0;
        g_npkgs++;
    }
    fclose(fp);
}

/* Resolution: account VOC (local overrides), then linked packages in
   listed order, then the system account's master VOC.  Foreign verbs
   execute by path from their own CATALOG but run in the user's
   account (cwd). */
static int voc_lookup(const char *verb, char *path, size_t cap) {
    if (g_voc_state == 0) g_voc_state = voc_open(&g_voc, "VOC");
    if (g_voc_state > 0 && voc_read(&g_voc, verb, path, cap))
        return 1;

    pkgs_reload();
    for (int i = 0; i < g_npkgs; i++) {
        if (g_pkgvoc_state[i] == 0) {
            char pv[1152];
            snprintf(pv, sizeof pv, "%s/VOC", g_pkgs[i]);
            g_pkgvoc_state[i] = voc_open(&g_pkgvoc[i], pv);
        }
        if (g_pkgvoc_state[i] > 0) {
            char rel[1024];
            if (voc_read(&g_pkgvoc[i], verb, rel, sizeof rel)) {
                snprintf(path, cap, "%s/%s", g_pkgs[i], rel);
                return 1;
            }
        }
    }

    if (g_sysvoc_state == 0) {
        char sysvoc[4096];
        snprintf(sysvoc, sizeof sysvoc, "%s/VOC", system_dir());
        g_sysvoc_state = voc_open(&g_sysvoc, sysvoc);
    }
    if (g_sysvoc_state > 0) {
        char rel[1024];
        if (voc_read(&g_sysvoc, verb, rel, sizeof rel)) {
            snprintf(path, cap, "%s/%s", system_dir(), rel);
            return 1;
        }
    }
    return (g_voc_state < 0 && g_sysvoc_state < 0) ? -1 : 0;
}

static void run_verb(const char *path, const char *line) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("mvx-tcl: fork");
        return;
    }
    if (pid == 0) {
        setenv("MVX_SENTENCE", line, 1);
        char *dup = strdup(line);
        char *argv[64];
        int n = 0;
        for (char *t = strtok(dup, " \t"); t && n < 63;
             t = strtok(NULL, " \t"))
            argv[n++] = t;
        argv[n] = NULL;
        execv(path, argv);
        fprintf(stderr, "mvx-tcl: cannot execute %s\n", path);
        _exit(127);
    }
    int st;
    waitpid(pid, &st, 0);
}

static void command(char *line) {
    while (*line == ' ' || *line == '\t') line++;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                       line[len - 1] == ' '))
        line[--len] = '\0';
    if (len == 0) return;

    if (line[0] == '!') {               /* raw Unix — runtime-gated */
        mvx_unix_cmd(g_ctx, line + 1);
        return;
    }

    char verb[128];
    size_t vn = 0;
    for (const char *p = line; *p && *p != ' ' && *p != '\t' &&
                               vn < sizeof verb - 1; p++)
        verb[vn++] = (char)toupper((unsigned char)*p);
    verb[vn] = '\0';

    if (strcmp(verb, "OFF") == 0 || strcmp(verb, "QUIT") == 0 ||
        strcmp(verb, "BYE") == 0)
        exit(0);

    if (strcmp(verb, "SH") == 0) {      /* interactive shell — gated */
        const char *sh = getenv("SHELL");
        mvx_unix_cmd(g_ctx, sh && sh[0] ? sh : "/bin/sh");
        return;
    }

    char path[1024];
    int r = voc_lookup(verb, path, sizeof path);
    if (r > 0) {
        run_verb(path, line);
        return;
    }
    if (r < 0)
        fprintf(stderr, "mvx-tcl: no VOC found in this account or the "
                        "system account (%s); only builtins are "
                        "available\n", system_dir());
    else
        fprintf(stderr, "verb \"%s\" not found\n", verb);
}

int main(int argc, char **argv) {
    const char *acct = NULL;
    const char *one_cmd = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 && i + 1 < argc)
            acct = argv[++i];
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            one_cmd = argv[++i];
        else {
            fprintf(stderr, "usage: mvx-tcl [-a account] [-c command]\n");
            return 2;
        }
    }

    /* Account resolution: -a flag, then $MVXACCOUNT, then cwd
       (ARCHITECTURE.md 7.2 — a parameter, not a mode). */
    if (!acct) acct = getenv("MVXACCOUNT");
    if (!acct || !acct[0]) acct = ".";
    if (chdir(acct) != 0) {
        fprintf(stderr, "mvx-tcl: cannot enter account %s\n", acct);
        return 1;
    }
    setenv("MVXACCOUNT", ".", 1);       /* children resolve from cwd */

    /* The session owns the select-list handoff file (7.3).  A nested
       TCL (spawned by EXECUTE) inherits the outer session rather than
       starting its own, so select lists flow across nesting. */
    static char sesspath[256];
    if (!getenv("MVXSESSION")) {
        snprintf(sesspath, sizeof sesspath, "/tmp/mvxsess.XXXXXX");
        int fd = mkstemp(sesspath);
        if (fd >= 0) {
            close(fd);
            setenv("MVXSESSION", sesspath, 1);
        }
    }

    g_ctx = mvx_ctx_create();

    if (one_cmd) {                      /* ssh/cron style: -c and out */
        char *dup = strdup(one_cmd);
        command(dup);
        free(dup);
        mvx_ctx_destroy(g_ctx);
        if (sesspath[0]) unlink(sesspath);
        return 0;
    }

    char cwd[4096] = "?";
    getcwd(cwd, sizeof cwd);
    const char *base = strrchr(cwd, '/');
    base = base && base[1] ? base + 1 : cwd;

    int tty = isatty(0);
    if (tty) printf("MVX TCL — account %s (%s)\n", base, cwd);
    char line[4096];
    for (;;) {
        if (tty) {
            printf("%s> ", base);
            fflush(stdout);
        }
        if (!fgets(line, sizeof line, stdin)) break;
        command(line);
    }
    if (tty) fputc('\n', stdout);
    mvx_ctx_destroy(g_ctx);
    if (sesspath[0]) unlink(sesspath);
    return 0;
}
