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
# Build an MVX package: compile BP/* into CATALOG/.  A package is an
# account-shaped directory (BP source, VOC verb records, CATALOG
# executables) that accounts link with LINK-PKG.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG="${1:?usage: mkpkg.sh <package-directory>}"

[ -d "$PKG/BP" ] || { echo "mkpkg: $PKG has no BP source directory" >&2; exit 1; }
# A subroutine-only package has an empty VOC; git does not track empty
# directories, so recreate it here rather than failing a fresh checkout.
mkdir -p "$PKG/VOC"

case "$(uname)" in
  Darwin) EXT=dylib ;;
  *)      EXT=so ;;
esac

PKGNAME="$(head -1 "$PKG/PKG" 2>/dev/null)"
[ -n "$PKGNAME" ] || PKGNAME="$(basename "$PKG")"

# SUBROUTINE sources bundle into ONE shared library per package (the
# jBASE deployment shape — one dlopen, one artifact); main programs
# become verb executables in CATALOG/.
mkdir -p "$PKG/CATALOG"
rm -rf "$PKG/LIB"

# native subroutine libraries (C using the subroutine ABI): a package
# with build-native.sh builds them into LIB/ before the BASIC subs.
if [ -x "$PKG/build-native.sh" ]; then
  "$PKG/build-native.sh"
fi
SUBS=""
for src in "$PKG"/BP/*; do
  [ -f "$src" ] || continue
  name="$(basename "$src")"
  # first real statement token, skipping *,!,REM,// line comments and
  # /* */ block comments (so docblocks in either style are ignored)
  first="$(awk '
    { line = $0 }
    inblk { if (line ~ /\*\//) { sub(/.*\*\//, "", line); inblk = 0 } else next }
    { sub(/^[ \t]+/, "", line) }
    line ~ /^\/\*/ { if (line !~ /\*\//) { inblk = 1 }; next }
    line ~ /^(\*|!|\/\/)/ { next }
    line ~ /^[Rr][Ee][Mm]([ \t]|$)/ { next }
    line ~ /^[ \t]*$/ { next }
    { n = split(line, a, /[ \t]/); print a[1]; exit }
  ' "$src")"
  if [ "$first" = "SUBROUTINE" ]; then
    SUBS="$SUBS $src"
    echo "  bundling $name"
  else
    "$ROOT/build/bin/mvx-basic" "$src" -o "$PKG/CATALOG/$name"
    echo "  cataloged $name"
  fi
done
if [ -n "$SUBS" ]; then
  mkdir -p "$PKG/LIB"
  # shellcheck disable=SC2086
  "$ROOT/build/bin/mvx-basic" -shared $SUBS -o "$PKG/LIB/lib$PKGNAME.$EXT"
  echo "  built LIB/lib$PKGNAME.$EXT"
fi

# bin/: standalone OS commands a package ships (built by build-native.sh
# or checked in).  Symlink them beside mvx so they land on the dev PATH;
# an install would copy them into a real bin directory instead.
if [ -d "$PKG/bin" ]; then
  mkdir -p "$ROOT/build/bin"
  for b in "$PKG"/bin/*; do
    [ -f "$b" ] && [ -x "$b" ] || continue
    babs="$(cd "$(dirname "$b")" && pwd)/$(basename "$b")"
    ln -sf "$babs" "$ROOT/build/bin/$(basename "$b")"
    echo "  installed bin/$(basename "$b") -> build/bin/$(basename "$b")"
  done
fi

echo "package built: $PKG"
