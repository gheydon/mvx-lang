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
"$ROOT/build/bin/mvx-tcl" -a "$ACCT" -c "CREATE-FILE VOC" >/dev/null
printf '# MVX account descriptor\nname = %s\nversion = 1\n' \
  "$(basename "$ACCT")" > "$ACCT/.mvx"

echo "account ready: $ACCT"
