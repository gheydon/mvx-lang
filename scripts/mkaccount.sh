#!/bin/sh
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
