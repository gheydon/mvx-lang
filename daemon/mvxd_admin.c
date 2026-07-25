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

/* mvx-lmdbd-admin — provision namespace credentials for mvx-lmdbd.
 *
 * A standalone tool for the daemon host: it manages the plain-text creds
 * list <datadir>/accounts, one line per namespace `name salt hash`, and
 * depends only on libc + a vendored SHA-256 — no MVX runtime, no LMDB.
 * Provisioning is offline and local: access to the data dir is the trust
 * boundary.  The token is generated here, hashed into the list, and
 * printed once; store it in the client's .mvx-private.  The daemon only
 * ever reads this file to verify AUTH; it is Pick-agnostic — a namespace
 * is just an opaque partition name.
 *
 *   mvx-lmdbd-admin -d <datadir> create-account <ns>
 *   mvx-lmdbd-admin -d <datadir> rotate <ns>
 *   mvx-lmdbd-admin -d <datadir> delete-account <ns>
 *   mvx-lmdbd-admin -d <datadir> list-accounts
 */
#include "sha256.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ACCTS 4096

static int ns_ok(const char *ns) {
    if (!ns || !ns[0] || strcmp(ns, ".") == 0 || strcmp(ns, "..") == 0)
        return 0;
    for (const char *c = ns; *c; c++)
        if (*c == '/' || *c == '\\' || *c == ' ' || *c == '\t') return 0;
    return 1;
}

static void hexof(const uint8_t *in, size_t n, char *out) {
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = hx[in[i] >> 4];
        out[i * 2 + 1] = hx[in[i] & 15];
    }
    out[n * 2] = '\0';
}

static int rand_hex(char *out, size_t nbytes) {
    uint8_t b[64];
    if (nbytes > sizeof b) return 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return 0;
    size_t got = 0;
    while (got < nbytes) {
        ssize_t r = read(fd, b + got, nbytes - got);
        if (r <= 0) { close(fd); return 0; }
        got += (size_t)r;
    }
    close(fd);
    hexof(b, nbytes, out);
    return 1;
}

/* Load the creds list as name/salt/hash rows. */
struct acct {
    char name[128], salt[64], hash[80];
};
static int load(const char *path, struct acct *a, int max) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    int n = 0;
    char ln[512];
    while (n < max && fgets(ln, sizeof ln, fp)) {
        if (ln[0] == '#' || ln[0] == '\n') continue;
        if (sscanf(ln, "%127s %63s %79s", a[n].name, a[n].salt,
                   a[n].hash) == 3)
            n++;
    }
    fclose(fp);
    return n;
}

/* Rewrite the creds list atomically, mode 0600. */
static int store(const char *path, struct acct *a, int n) {
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return 0;
    FILE *fp = fdopen(fd, "w");
    if (!fp) { close(fd); return 0; }
    for (int i = 0; i < n; i++)
        fprintf(fp, "%s %s %s\n", a[i].name, a[i].salt, a[i].hash);
    fclose(fp);
    if (rename(tmp, path) != 0) { unlink(tmp); return 0; }
    chmod(path, 0600);
    return 1;
}

static int find(struct acct *a, int n, const char *ns) {
    for (int i = 0; i < n; i++)
        if (strcmp(a[i].name, ns) == 0) return i;
    return -1;
}

/* create or rotate: issue a token, store salt+hash, print the token. */
static int provision(const char *path, const char *ns, int rotate) {
    static struct acct a[MAX_ACCTS];
    int n = load(path, a, MAX_ACCTS);
    int at = find(a, n, ns);
    if (at >= 0 && !rotate) {
        fprintf(stderr, "mvx-lmdbd-admin: account '%s' already exists\n", ns);
        return 2;
    }
    if (at < 0 && rotate) {
        fprintf(stderr, "mvx-lmdbd-admin: no such account '%s'\n", ns);
        return 2;
    }
    char salt[33], token[65], hash[65];
    if (!rand_hex(salt, 16) || !rand_hex(token, 32)) {
        fprintf(stderr, "mvx-lmdbd-admin: cannot read /dev/urandom\n");
        return 1;
    }
    sha256_salted_hex(salt, token, hash);
    if (at < 0) { at = n++; snprintf(a[at].name, sizeof a[at].name, "%s", ns); }
    snprintf(a[at].salt, sizeof a[at].salt, "%s", salt);
    snprintf(a[at].hash, sizeof a[at].hash, "%s", hash);
    if (!store(path, a, n)) {
        fprintf(stderr, "mvx-lmdbd-admin: cannot write %s\n", path);
        return 1;
    }
    printf("%s\n", token);            /* the one time the token is shown */
    fprintf(stderr, "account '%s' %s; store the token above in the "
                    "client's .mvx-private\n",
            ns, rotate ? "rotated" : "created");
    return 0;
}

static int delete_account(const char *path, const char *ns) {
    static struct acct a[MAX_ACCTS];
    int n = load(path, a, MAX_ACCTS);
    int at = find(a, n, ns);
    if (at < 0) {
        fprintf(stderr, "mvx-lmdbd-admin: no such account '%s'\n", ns);
        return 2;
    }
    a[at] = a[--n];
    if (!store(path, a, n)) return 1;
    fprintf(stderr, "account '%s' deleted (its data dir is left in place)\n",
            ns);
    return 0;
}

static int list_accounts(const char *path) {
    static struct acct a[MAX_ACCTS];
    int n = load(path, a, MAX_ACCTS);
    for (int i = 0; i < n; i++) printf("%s\n", a[i].name);
    return 0;
}

int main(int argc, char **argv) {
    const char *datadir = NULL;
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
            datadir = argv[++i];
        else
            break;
    }
    const char *cmd = i < argc ? argv[i++] : NULL;
    const char *ns = i < argc ? argv[i++] : NULL;
    if (!datadir || !cmd) {
        fprintf(stderr,
                "usage: mvx-lmdbd-admin -d <datadir> "
                "create-account|rotate|delete-account <ns> | list-accounts\n");
        return 2;
    }
    char path[4096];
    snprintf(path, sizeof path, "%s/accounts", datadir);
    mkdir(datadir, 0775);

    if (strcmp(cmd, "list-accounts") == 0) return list_accounts(path);
    if (!ns || !ns_ok(ns)) {
        fprintf(stderr, "mvx-lmdbd-admin: invalid or missing account name\n");
        return 2;
    }
    if (strcmp(cmd, "create-account") == 0) return provision(path, ns, 0);
    if (strcmp(cmd, "rotate") == 0) return provision(path, ns, 1);
    if (strcmp(cmd, "delete-account") == 0) return delete_account(path, ns);
    fprintf(stderr, "mvx-lmdbd-admin: unknown command '%s'\n", cmd);
    return 2;
}
