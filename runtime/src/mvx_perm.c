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

/* Fine-grained OS-command permissions (ARCHITECTURE.md 8.4).
 *
 * The privilege tiers (mvx_exec.c) are all-or-nothing: raw Unix needs the
 * whole `unrestricted` tier — full shell.  This adds a narrow path: a
 * program may run a SPECIFIC external command (argv-style, never a shell)
 * when a grant permits it, without unrestricted.
 *
 * Grants are group-scoped whitelist/denylist lines:
 *
 *     permit <group> = mkdir tar rm         # this group may run these
 *     permit *       = uname                 # * = any user/group
 *     deny   <group> = rm -r -R --recursive  # ...but never rm WITH these switches
 *     deny   *       = shutdown               # ...and never this command at all
 *
 * A `permit` grants a command (any arguments).  A `deny` is a hard override
 * that wins over any permit: with no switches it forbids the command outright,
 * with switches it forbids the command only when one of those switches is
 * present.  Switches match by identity, INCLUDING bundled short options — a
 * deny of `-r` also blocks `-fr` / `-rf` — and long options (`--recursive`,
 * `--recursive=…`).  A command is matched by basename (`tar` covers `/bin/tar`).
 *
 * Grants come from three files, unioned, in increasing authority:
 *   1. `<account>/.mvx`                     — the packager's declaration.
 *   2. `<account>/.mvx-private/permissions` — the account's site policy
 *                                             (git-ignored, fs-protected).
 *   3. `<system>/.mvx-private/permissions`  — the SYSTEM-account layer: the
 *                                             admin's per-user/group override,
 *                                             outside every account so it cannot
 *                                             be self-granted (8.3).  <system>
 *                                             is $MVXSYSTEM (else MVX_SYSTEM_DIR).
 * A `deny` anywhere wins over any `permit`, so the system layer can lock a
 * command (or a switch) down for a user/group regardless of what an account
 * grants itself; the account files can only NARROW, not escalate past a system
 * deny.  A grant's `<group>` matches the caller's OS groups OR their username
 * (so the system layer can scope per user), plus `*` for anyone.
 */
#include "mvx_runtime.h"

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_RULES 512
#define MAX_GROUPS 256
#define GRP_LEN 64
#define CMD_LEN 128
#define SHORTS_LEN 64
#define MAX_LONGS 8
#define LONG_LEN 48

/* A rule: a group, a command (basename), and — for a deny with switches — the
   short-option letters and long-option names it forbids (empty => the whole
   command). */
struct rule {
    char group[GRP_LEN];
    char cmd[CMD_LEN];
    char shorts[SHORTS_LEN];
    char longs[MAX_LONGS][LONG_LEN];
    int nlongs;
    int has_switches;
};

static struct rule permits[MAX_RULES];
static int npermits = 0;
static struct rule denies[MAX_RULES];
static int ndenies = 0;
static char mygroups[MAX_GROUPS][GRP_LEN];
static int nmygroups = 0;
static int loaded = 0;

static const char *acct_root(void) {
    const char *a = getenv("MVXACCOUNT");
    return (a && a[0]) ? a : ".";
}

/* Copy the next whitespace/comma-delimited token; advance *p.  0 if none. */
static int next_tok(const char **p, char *out, size_t cap) {
    const char *s = *p;
    while (*s == ' ' || *s == '\t' || *s == ',') s++;
    const char *e = s;
    while (*e && *e != ' ' && *e != '\t' && *e != ',' && *e != '\n' && *e != '\r') e++;
    size_t n = (size_t)(e - s);
    if (n == 0) { *p = e; return 0; }
    if (n >= cap) n = cap - 1;
    memcpy(out, s, n);
    out[n] = '\0';
    *p = e;
    return 1;
}

/* Add one switch token ("-r", "-fr", "--recursive", "--foo=bar") to a rule. */
static void rule_add_switch(struct rule *r, const char *sw) {
    if (sw[0] == '-' && sw[1] == '-') {                 /* long option */
        char nm[LONG_LEN];
        int n = 0;
        for (const char *c = sw + 2; *c && *c != '=' && n < LONG_LEN - 1; c++) nm[n++] = *c;
        nm[n] = '\0';
        if (n && r->nlongs < MAX_LONGS) { snprintf(r->longs[r->nlongs++], LONG_LEN, "%s", nm); r->has_switches = 1; }
    } else if (sw[0] == '-' && sw[1]) {                 /* short option(s) */
        size_t have = strlen(r->shorts);
        for (const char *c = sw + 1; *c && have < SHORTS_LEN - 1; c++)
            if (!strchr(r->shorts, *c)) { r->shorts[have++] = *c; r->shorts[have] = '\0'; r->has_switches = 1; }
    }
}

static void parse_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    char line[4096];
    while (fgets(line, sizeof line, fp)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        int is_permit = strncmp(p, "permit", 6) == 0 && (p[6] == ' ' || p[6] == '\t');
        int is_deny = strncmp(p, "deny", 4) == 0 && (p[4] == ' ' || p[4] == '\t');
        if (!is_permit && !is_deny) continue;
        p += is_permit ? 6 : 4;
        char grp[GRP_LEN];
        if (!next_tok(&p, grp, sizeof grp)) continue;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        if (is_permit) {
            /* whole commands, any arguments */
            char cmd[CMD_LEN];
            while (next_tok(&p, cmd, sizeof cmd)) {
                if (npermits >= MAX_RULES) break;
                struct rule *r = &permits[npermits++];
                memset(r, 0, sizeof *r);
                snprintf(r->group, GRP_LEN, "%s", grp);
                snprintf(r->cmd, CMD_LEN, "%s", cmd);
            }
        } else {
            /* one command, then the switches it is denied with (none = all) */
            char cmd[CMD_LEN];
            if (!next_tok(&p, cmd, sizeof cmd)) continue;
            if (ndenies >= MAX_RULES) continue;
            struct rule *r = &denies[ndenies++];
            memset(r, 0, sizeof *r);
            snprintf(r->group, GRP_LEN, "%s", grp);
            snprintf(r->cmd, CMD_LEN, "%s", cmd);
            char sw[LONG_LEN + 4];
            while (next_tok(&p, sw, sizeof sw)) rule_add_switch(r, sw);
        }
    }
    fclose(fp);
}

/* The caller's OS groups, by name (supplementary + primary). */
static void load_groups(void) {
    int ng = getgroups(0, NULL);
    gid_t *gids = NULL;
    if (ng > 0) {
        gids = malloc((size_t)ng * sizeof(gid_t));
        if (gids && getgroups(ng, gids) < 0) ng = 0;
    } else {
        ng = 0;
    }
    struct passwd *pw = getpwuid(getuid());
    gid_t primary = pw ? pw->pw_gid : getgid();
    int have_primary = 0;
    for (int i = 0; i < ng; i++) {
        if (gids[i] == primary) have_primary = 1;
        struct group *gr = getgrgid(gids[i]);
        if (gr && gr->gr_name && nmygroups < MAX_GROUPS)
            snprintf(mygroups[nmygroups++], GRP_LEN, "%s", gr->gr_name);
    }
    if (!have_primary && nmygroups < MAX_GROUPS) {
        struct group *gr = getgrgid(primary);
        if (gr && gr->gr_name) snprintf(mygroups[nmygroups++], GRP_LEN, "%s", gr->gr_name);
    }
    /* the username is also an identity a grant may name (per-user scoping) */
    if (pw && pw->pw_name && nmygroups < MAX_GROUPS)
        snprintf(mygroups[nmygroups++], GRP_LEN, "%s", pw->pw_name);
    free(gids);
}

static void load(void) {
    if (loaded) return;
    loaded = 1;
    char path[4096];
    snprintf(path, sizeof path, "%s/.mvx", acct_root());
    parse_file(path);
    snprintf(path, sizeof path, "%s/.mvx-private/permissions", acct_root());
    parse_file(path);
    /* the system-account layer — the admin's authoritative per-user/group
       override, outside every account (8.3). */
    const char *sys = getenv("MVXSYSTEM");
#ifdef MVX_SYSTEM_DIR
    if (!sys || !sys[0]) sys = MVX_SYSTEM_DIR;
#endif
    if (sys && sys[0]) {
        snprintf(path, sizeof path, "%s/.mvx-private/permissions", sys);
        parse_file(path);
    }
    load_groups();
}

static int in_my_groups(const char *g) {
    if (strcmp(g, "*") == 0) return 1;
    for (int i = 0; i < nmygroups; i++)
        if (strcmp(mygroups[i], g) == 0) return 1;
    return 0;
}

static const char *base_of(const char *cmd) {
    const char *slash = strrchr(cmd, '/');
    return slash ? slash + 1 : cmd;
}

/* Does argv element `e` (a switch) match one of the rule's forbidden switches?
   Handles bundled shorts (-fr matches a denied -r) and long options. */
static int switch_hits(const struct rule *r, const char *e) {
    if (e[0] != '-' || e[1] == '\0') return 0;          /* not a switch ("-" is stdin) */
    if (e[1] == '-') {                                  /* long: --name[=val] */
        if (e[2] == '\0') return 0;                     /* "--" ends options */
        char nm[LONG_LEN];
        int n = 0;
        for (const char *c = e + 2; *c && *c != '=' && n < LONG_LEN - 1; c++) nm[n++] = *c;
        nm[n] = '\0';
        for (int i = 0; i < r->nlongs; i++)
            if (strcmp(nm, r->longs[i]) == 0) return 1;
        return 0;
    }
    for (const char *c = e + 1; *c; c++)                /* bundled short letters */
        if (strchr(r->shorts, *c)) return 1;
    return 0;
}

/* 1 if the caller's groups may run argv[0] with these arguments; a permit must
   grant the command AND no matching deny rule may fire. */
int mvx_perm_allowed(char *const argv[]) {
    if (!argv || !argv[0] || !argv[0][0]) return 0;
    load();
    const char *base = base_of(argv[0]);

    int permitted = 0;
    for (int i = 0; i < npermits && !permitted; i++)
        if (strcmp(permits[i].cmd, base) == 0 && in_my_groups(permits[i].group))
            permitted = 1;
    if (!permitted) return 0;

    for (int i = 0; i < ndenies; i++) {
        const struct rule *r = &denies[i];
        if (strcmp(r->cmd, base) != 0 || !in_my_groups(r->group)) continue;
        if (!r->has_switches) return 0;                 /* whole-command deny */
        for (int a = 1; argv[a]; a++)
            if (switch_hits(r, argv[a])) return 0;      /* a forbidden switch is present */
    }
    return 1;
}
