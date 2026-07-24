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
# Convert a freshly cloned MVX account (legible directory files, as
# written by EXPORT-ACCOUNT and committed to git) into a working account
# backed by hash files: VOC and reference data move into LMDB, bulk data
# files are recreated empty from their dictionaries, BP source is
# cataloged, and packages are linked.  Developer privilege is needed for
# the cataloging step.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ACCT="${1:?usage: mvx-convert.sh <account-directory>}"

[ -d "$ACCT" ] || { echo "mvx-convert: $ACCT does not exist" >&2; exit 1; }

MVXPRIV=developer "$ROOT/build/bin/mvx-tcl" -a "$ACCT" -c "CONVERT-ACCOUNT"
echo "converted: $ACCT"
