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

mkdir -p "$PKG/CATALOG"
for src in "$PKG"/BP/*; do
  [ -f "$src" ] || continue
  name="$(basename "$src")"
  # SUBROUTINE sources become shared libraries in LIB/ (runtime CALL
  # loads them); main programs become verb executables in CATALOG/.
  first="$(awk 'NF && $1 !~ /^[*!]/ { print $1; exit }' "$src")"
  if [ "$first" = "SUBROUTINE" ]; then
    mkdir -p "$PKG/LIB"
    "$ROOT/build/bin/mvx" -shared "$src" -o "$PKG/LIB/$name.$EXT"
    echo "  cataloged $name (subroutine)"
  else
    "$ROOT/build/bin/mvx" "$src" -o "$PKG/CATALOG/$name"
    echo "  cataloged $name"
  fi
done

echo "package built: $PKG"
