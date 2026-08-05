#!/bin/sh
# MVX — a native compiler and runtime for Pick/MultiValue BASIC.
# Copyright (C) 2026 Gordon Heydon.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2, as
# published by the Free Software Foundation.  There is NO WARRANTY, to
# the extent permitted by law; see the LICENSE file for details.
#
# SPDX-License-Identifier: GPL-2.0-only
# Create an MVX account.  Standard verbs come from the system account
# (built into build/system, overridable with $MVXSYSTEM); the account
# itself gets only an empty local VOC for its own cataloged programs.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ACCT="${1:?usage: mkaccount.sh <account-directory>}"

mkdir -p "$ACCT"
"$ROOT/build/bin/mvx" -a "$ACCT" -c "CREATE-FILE VOC" >/dev/null
printf '# MVX account descriptor\nname = %s\nversion = 1\n' \
  "$(basename "$ACCT")" > "$ACCT/.mvx"

# Seed the account's default OS-command permissions from the system account's
# .mvx (its `permit`/`deny` lines), so a new account starts with the site
# baseline; per-account and system-layer policy then layer on top (see
# ARCHITECTURE.md 8.4).
SYS="${MVXSYSTEM:-$ROOT/build/system}"
if [ -f "$SYS/.mvx" ]; then
  grep -E '^[[:space:]]*(permit|deny)[[:space:]]' "$SYS/.mvx" >> "$ACCT/.mvx" || true
fi

echo "account ready: $ACCT"
