#!/bin/sh
# Build an MVX package: compile BP/* into CATALOG/.  A package is an
# account-shaped directory (BP source, VOC verb records, CATALOG
# executables) that accounts link with LINK-PKG.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG="${1:?usage: mkpkg.sh <package-directory>}"

[ -d "$PKG/BP" ] || { echo "mkpkg: $PKG has no BP source directory" >&2; exit 1; }
[ -d "$PKG/VOC" ] || { echo "mkpkg: $PKG has no VOC directory" >&2; exit 1; }

mkdir -p "$PKG/CATALOG"
for src in "$PKG"/BP/*; do
  [ -f "$src" ] || continue
  name="$(basename "$src")"
  "$ROOT/build/bin/mvx" "$src" -o "$PKG/CATALOG/$name"
  echo "  cataloged $name"
done

echo "package built: $PKG"
