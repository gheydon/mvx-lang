#!/bin/sh
# Build an MVX package: compile BP/* into CATALOG/.  A package is an
# account-shaped directory (BP source, VOC verb records, CATALOG
# executables) that accounts link with LINK-PKG.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG="${1:?usage: mkpkg.sh <package-directory>}"

[ -d "$PKG/BP" ] || { echo "mkpkg: $PKG has no BP source directory" >&2; exit 1; }
[ -d "$PKG/VOC" ] || { echo "mkpkg: $PKG has no VOC directory" >&2; exit 1; }

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
SUBS=""
for src in "$PKG"/BP/*; do
  [ -f "$src" ] || continue
  name="$(basename "$src")"
  first="$(awk 'NF && $1 !~ /^[*!]/ { print $1; exit }' "$src")"
  if [ "$first" = "SUBROUTINE" ]; then
    SUBS="$SUBS $src"
    echo "  bundling $name"
  else
    "$ROOT/build/bin/mvx" "$src" -o "$PKG/CATALOG/$name"
    echo "  cataloged $name"
  fi
done
if [ -n "$SUBS" ]; then
  mkdir -p "$PKG/LIB"
  # shellcheck disable=SC2086
  "$ROOT/build/bin/mvx" -shared $SUBS -o "$PKG/LIB/lib$PKGNAME.$EXT"
  echo "  built LIB/lib$PKGNAME.$EXT"
fi

echo "package built: $PKG"
