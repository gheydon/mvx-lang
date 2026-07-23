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
#include <sys/wait.h>
#include <unistd.h>

static mvx_ctx *g_ctx;
static mv_value g_voc;
static int g_voc_state;                 /* 0 untried, 1 open, -1 absent */

static int voc_lookup(const char *verb, char *path, size_t cap) {
    if (g_voc_state == 0) {
        mv_value spec;
        mv_init(&spec);
        mv_set_str(&spec, "VOC", 3);
        mv_init(&g_voc);
        g_voc_state = mvx_open(g_ctx, NULL, &spec, &g_voc) ? 1 : -1;
        mv_clear(&spec);
    }
    if (g_voc_state < 0) return -1;

    mv_value id, rec, a1, a2;
    mv_init(&id); mv_init(&rec); mv_init(&a1); mv_init(&a2);
    mv_set_str(&id, verb, (int64_t)strlen(verb));
    int found = 0;
    if (mvx_read(g_ctx, &rec, &g_voc, &id, 0)) {
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
        fprintf(stderr, "mvx-tcl: this account has no VOC "
                        "(run the account bootstrap); only builtins "
                        "are available\n");
    else
        fprintf(stderr, "verb \"%s\" not found in VOC\n", verb);
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

    g_ctx = mvx_ctx_create();

    if (one_cmd) {                      /* ssh/cron style: -c and out */
        char *dup = strdup(one_cmd);
        command(dup);
        free(dup);
        mvx_ctx_destroy(g_ctx);
        return 0;
    }

    int tty = isatty(0);
    if (tty) printf("MVX TCL\n");
    char line[4096];
    for (;;) {
        if (tty) {
            fputs("> ", stdout);
            fflush(stdout);
        }
        if (!fgets(line, sizeof line, stdin)) break;
        command(line);
    }
    if (tty) fputc('\n', stdout);
    mvx_ctx_destroy(g_ctx);
    return 0;
}
