#!/bin/sh
# Create an MVX account: compile the base verbs into CATALOG/ and seed
# the VOC.  Usage: scripts/mkaccount.sh <account-directory>
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MVX="$ROOT/build/bin/mvx"
ACCT="${1:?usage: mkaccount.sh <account-directory>}"

mkdir -p "$ACCT/CATALOG"

for src in "$ROOT"/verbs/*.b; do
  name="$(basename "$src" .b)"
  "$MVX" "$src" -o "$ACCT/CATALOG/$name"
done

"$MVX" "$ROOT/scripts/setup-voc.b" -o "$ACCT/CATALOG/.setup-voc"
(cd "$ACCT" && MVXACCOUNT=. "./CATALOG/.setup-voc")

echo "account ready: $ACCT"
