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

/* Spawning and the privilege gate.
 *
 * THE GATE LIVES HERE, IN THE RUNTIME (ARCHITECTURE.md 8.1): anyone who
 * can compile can call these primitives directly, so a check anywhere
 * shallower would be decorative.  One gate covers every path — the TCL
 * `!` builtin, EXECUTE, and the compiler spawn.
 *
 * Tiers (8.2): restricted < developer < unrestricted, default DENY.
 * Spawning cataloged verbs inside the account is the platform working
 * and is allowed at every tier; raw Unix needs unrestricted; compiling
 * needs developer.  The tier comes from $MVXPRIV — the development
 * stand-in for system-level configuration outside the account (8.3);
 * an env var set by the login environment is not writable from inside
 * account data, which is the property that matters.
 *
 * All spawns are argv-style (execv), never through a shell, except the
 * explicitly-unrestricted raw Unix passthrough: with argument vectors,
 * metacharacters in user input are inert (8.4).
 */
#include "mvx_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define AM ((char)0xFE)

#ifndef MVX_BIN_DIR
#define MVX_BIN_DIR "."
#endif

enum { TIER_RESTRICTED = 0, TIER_DEVELOPER = 1, TIER_UNRESTRICTED = 2 };

static int priv_tier(void) {
    const char *p = getenv("MVXPRIV");
    if (!p) return TIER_RESTRICTED;                 /* default deny */
    if (strcasecmp(p, "unrestricted") == 0) return TIER_UNRESTRICTED;
    if (strcasecmp(p, "developer") == 0) return TIER_DEVELOPER;
    return TIER_RESTRICTED;
}

static const char *bin_dir(void) {
    const char *p = getenv("MVXBIN");
    return p && p[0] ? p : MVX_BIN_DIR;
}

/* Spawn argv, optionally capturing stdout into a dynamic array
   (newlines become attribute marks).  Returns the exit status, or -1
   on spawn failure. */
static int64_t spawn(char *const argv[], mv_value *capture) {
    int fds[2] = {-1, -1};
    if (capture && pipe(fds) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (capture) {
            close(fds[0]);
            dup2(fds[1], 1);
            close(fds[1]);
        }
        execv(argv[0], argv);
        fprintf(stderr, "mvx: cannot execute %s\n", argv[0]);
        _exit(127);
    }

    if (capture) {
        close(fds[1]);
        char *buf = NULL;
        size_t len = 0, cap = 0;
        char chunk[4096];
        ssize_t n;
        while ((n = read(fds[0], chunk, sizeof chunk)) > 0) {
            if (len + (size_t)n > cap) {
                cap = cap ? cap * 2 : 8192;
                while (cap < len + (size_t)n) cap *= 2;
                char *nb = realloc(buf, cap);
                if (!nb) mvx_fatal("out of memory in EXECUTE capture");
                buf = nb;
            }
            memcpy(buf + len, chunk, (size_t)n);
            len += (size_t)n;
        }
        close(fds[0]);
        while (len > 0 && buf[len - 1] == '\n') len--;
        for (size_t i = 0; i < len; i++)
            if (buf[i] == '\n') buf[i] = AM;
        mv_set_str(capture, buf ? buf : "", (int64_t)len);
        free(buf);
    }

    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* --- raw Unix (the `!` / SH path) — unrestricted only ------------------ */

int64_t mvx_unix_cmd(mvx_ctx *ctx, const char *cmd) {
    (void)ctx;
    if (priv_tier() < TIER_UNRESTRICTED) {
        fprintf(stderr,
                "not allowed: Unix execution requires unrestricted "
                "privilege\n");
        return -1;
    }
    int st = system(cmd);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* --- editor spawn (the VI verb) — unrestricted --------------------------
   Runs the configured interactive editor ($MVXEDITOR, then $VISUAL,
   then $EDITOR, then vi) on one file, argv-style.  It is a general
   exec (an editor's own shell escapes make it one), so it needs the
   unrestricted tier — the built-in ED remains the tier-safe editor. */
int64_t mvx_editfile(mvx_ctx *ctx, const mv_value *path) {
    (void)ctx;
    if (priv_tier() < TIER_UNRESTRICTED) {
        fprintf(stderr,
                "not allowed: external editing requires unrestricted "
                "privilege (use ED instead)\n");
        return -1;
    }
    const char *ed = getenv("MVXEDITOR");
    if (!ed || !ed[0]) ed = getenv("VISUAL");
    if (!ed || !ed[0]) ed = getenv("EDITOR");
    if (!ed || !ed[0]) ed = "vi";

    char nb[40];
    const char *pp;
    int64_t pl = mv_val_chars(path, nb, sizeof nb, &pp);
    char pathbuf[4096];
    snprintf(pathbuf, sizeof pathbuf, "%.*s", (int)pl, pp);

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[] = {(char *)ed, pathbuf, NULL};
        execvp(ed, argv);
        _exit(127);
    }
    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* TMPNAM() -> a unique temp path (the file is not created). */
void mvx_tmpnam(mv_value *dst) {
    const char *dir = getenv("TMPDIR");
    if (!dir || !dir[0]) dir = "/tmp";
    char tmpl[4096];
    snprintf(tmpl, sizeof tmpl, "%s/mvxedit.XXXXXX", dir);
    int fd = mkstemp(tmpl);
    if (fd >= 0) close(fd);             /* claim the name, keep the path */
    mv_set_str(dst, tmpl, (int64_t)strlen(tmpl));
}

/* --- compile — the narrow primitive (developer+) -----------------------
   Takes structured arguments and builds the argv itself: there is
   nothing to inject (8.2). mode: "c" object, "exe" executable,
   "shared" subroutine library. */

int64_t mvx_compile(mvx_ctx *ctx, const mv_value *mode,
                    const mv_value *src, const mv_value *out) {
    (void)ctx;
    if (priv_tier() < TIER_DEVELOPER) {
        fprintf(stderr,
                "not allowed: compiling requires developer privilege\n");
        return -1;
    }
    char mb[40], sb[40], ob[40];
    const char *mp, *sp, *op;
    mv_val_chars(mode, mb, sizeof mb, &mp);
    int64_t sl = mv_val_chars(src, sb, sizeof sb, &sp);
    int64_t ol = mv_val_chars(out, ob, sizeof ob, &op);
    if (sl == 0 || ol == 0) return -1;

    char mvx[4096], srcbuf[1024], outbuf[1024];
    snprintf(mvx, sizeof mvx, "%s/mvx", bin_dir());
    snprintf(srcbuf, sizeof srcbuf, "%.*s", (int)sl, sp);
    snprintf(outbuf, sizeof outbuf, "%.*s", (int)ol, op);
    if (mp[0] == 's' || mp[0] == 'S') {
        /* Shared subroutine libraries get the platform suffix so BASIC
           callers stay portable. */
        const char *slash = strrchr(outbuf, '/');
        if (!strchr(slash ? slash : outbuf, '.')) {
#ifdef __APPLE__
            strncat(outbuf, ".dylib", sizeof outbuf - strlen(outbuf) - 1);
#else
            strncat(outbuf, ".so", sizeof outbuf - strlen(outbuf) - 1);
#endif
        }
    }

    char *argv[8];
    int n = 0;
    argv[n++] = mvx;
    if (mp[0] == 'c' || mp[0] == 'C') argv[n++] = "-c";
    else if (mp[0] == 's' || mp[0] == 'S') argv[n++] = "-shared";
    argv[n++] = srcbuf;
    argv[n++] = "-o";
    argv[n++] = outbuf;
    argv[n] = NULL;
    return spawn(argv, NULL);
}

/* --- EXECUTE — run a TCL sentence (allowed at every tier) --------------
   Dispatch stays in one place: spawn mvx-tcl -c <sentence>.  A `!` in
   the sentence is gated inside the child by this same module. */

int64_t mvx_execute(mvx_ctx *ctx, const mv_value *sentence,
                    mv_value *capture, mv_value *rc) {
    (void)ctx;
    char nb[40];
    const char *sp;
    int64_t sl = mv_val_chars(sentence, nb, sizeof nb, &sp);

    char tcl[4096], sent[4096];
    snprintf(tcl, sizeof tcl, "%s/mvx-tcl", bin_dir());
    snprintf(sent, sizeof sent, "%.*s", (int)sl, sp);

    char *argv[5];
    argv[0] = tcl;
    argv[1] = "-c";
    argv[2] = sent;
    argv[3] = NULL;
    int64_t st = spawn(argv, capture);
    if (rc) mv_set_int(rc, st);
    return st == 0;
}
