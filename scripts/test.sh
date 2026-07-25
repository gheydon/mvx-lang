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
# MVX test harness.
#
#   scripts/test.sh            run everything
#   scripts/test.sh -q        quick: skip the 5-second sieve benchmark
#   scripts/test.sh --bless   (re)capture expected outputs — review the
#                             diff before committing!
#
# Phase 1 runs tests/*.b and diffs against tests/expected/<name>.out.
# Phase 2 runs scripted TCL sessions in a throwaway account and diffs
# against tests/expected/tcl-<name>.out (paths normalised).
# Phase 3 checks sieve correctness.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MVX="$ROOT/build/bin/mvx-basic"
TCL="$ROOT/build/bin/mvx"
EXP="$ROOT/tests/expected"

BLESS=0
QUICK=0
for a in "$@"; do
  case "$a" in
    --bless) BLESS=1 ;;
    -q) QUICK=1 ;;
    *) echo "usage: test.sh [-q] [--bless]" >&2; exit 2 ;;
  esac
done

[ -x "$MVX" ] || { echo "test.sh: build first (ninja -C build)" >&2; exit 2; }
mkdir -p "$EXP"

PASS=0
FAIL=0
TESTROOT="$(mktemp -d /tmp/mvxtest.XXXXXX)"
trap 'rm -rf "$TESTROOT"' EXIT

unset MVXACCOUNT MVXSESSION MVXPRIV MVXSYSTEM MVXPKGPATH MVXDRIVERS \
      MVX_SENTENCE 2>/dev/null || true

check() { # name actual-text
  name="$1"
  actual="$2"
  if [ "$BLESS" = 1 ]; then
    printf '%s\n' "$actual" > "$EXP/$name.out"
    echo "  blessed $name"
    return
  fi
  if [ ! -f "$EXP/$name.out" ]; then
    echo "FAIL $name: no expected output (run --bless)"
    FAIL=$((FAIL + 1))
    return
  fi
  if printf '%s\n' "$actual" | diff -u "$EXP/$name.out" - >"$TESTROOT/d" 2>&1
  then
    PASS=$((PASS + 1))
  else
    echo "FAIL $name:"
    sed 's/^/    /' "$TESTROOT/d" | head -20
    FAIL=$((FAIL + 1))
  fi
}

normalise() {
  sed -e "s#$TESTROOT#@TESTROOT@#g" -e "s#$ROOT#@ROOT@#g"
}

# ---------------------------------------------------------------- phase 1
echo "== language tests"

lang() { # name [stdin-text]
  name="$1"
  stdin="${2-}"
  out="$TESTROOT/$name"
  if ! "$MVX" "$ROOT/tests/$name.b" -o "$out" 2>"$TESTROOT/cerr"; then
    check "$name" "COMPILE FAILED: $(cat "$TESTROOT/cerr")"
    return
  fi
  if [ -n "$stdin" ]; then
    actual="$(printf '%s\n' "$stdin" | "$out" 2>&1 | normalise)"
  else
    actual="$("$out" 2>&1 | normalise)"
  fi
  check "$name" "$actual"
}

lang smoke
lang goto
lang case
lang dynarr
lang conv
lang strfns world
lang strmath
lang ifdef
lang equate
lang matparse
lang include
lang matches
lang loopctl
lang assign
lang vector

# KEYIN decoding from piped bytes (printable, specials, escape
# sequences, ESC pushback)
out="$TESTROOT/keyin"
if "$MVX" "$ROOT/tests/keyin.b" -o "$out" 2>"$TESTROOT/cerr"; then
  actual="$(printf 'a\r\t\177\033[A\033OB\033[3~\033OP\033[24~\001\033q' | \
            "$out" 2>&1)"
  check keyin "$actual"
else
  check keyin "COMPILE FAILED: $(cat "$TESTROOT/cerr")"
fi

# CALL across separately compiled sources
out="$TESTROOT/callmain"
"$MVX" "$ROOT/tests/callmain.b" "$ROOT/tests/adder.b" -o "$out" \
  2>/dev/null && check callmain "$("$out" 2>&1)" \
  || check callmain "COMPILE FAILED"

# storage tests run inside their own account directory
STACCT="$TESTROOT/stacct"
mkdir -p "$STACCT/DIRDATA"
for t in store storedir dict matread readv locked onerror; do
  out="$TESTROOT/$t"
  if "$MVX" "$ROOT/tests/$t.b" -o "$out" 2>"$TESTROOT/cerr"; then
    actual="$(cd "$STACCT" && MVXACCOUNT=. "$out" 2>&1 | normalise)"
    check "$t" "$actual"
  else
    check "$t" "COMPILE FAILED: $(cat "$TESTROOT/cerr")"
  fi
done

# ---------------------------------------------------------------- phase 2
echo "== system tests"

ACCT="$TESTROOT/acct"
"$ROOT/scripts/mkaccount.sh" "$ACCT" >/dev/null

tclrun() { # stdin piped in by caller
  "$TCL" -a "$ACCT" 2>&1 | normalise
}

# file lifecycle through the verbs
check tcl-files "$(printf '%s\n' \
  'CREATE-FILE STOCKF' \
  'COUNT STOCKF' \
  'LISTF' \
  'DELETE-FILE STOCKF' \
  'LISTF' | tclrun)"

# seed a queryable file with a dictionary
seed="$TESTROOT/seed.b"
cat > "$seed" <<'EOF'
X = CREATEFILE("PARTS")
OPEN "PARTS" TO F ELSE STOP
WRITE "Widget":@AM:"999":@AM:"blue" ON F, "W100"
WRITE "Gadget":@AM:"450":@AM:"red" ON F, "G200"
WRITE "Sprocket":@AM:"125":@AM:"blue" ON F, "S300"
OPEN "DICT", "PARTS" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Name":@AM:"12L" ON D, "NAME"
WRITE "D":@AM:"2":@AM:"MD2$":@AM:"Price":@AM:"10R" ON D, "PRICE"
WRITE "D":@AM:"3":@AM:"":@AM:"Colour":@AM:"8L" ON D, "COLOR"
PRINT "seeded"
EOF
"$MVX" "$seed" -o "$TESTROOT/seedbin" 2>/dev/null
(cd "$ACCT" && MVXACCOUNT=. "$TESTROOT/seedbin") >/dev/null

check tcl-query "$(printf '%s\n' \
  'LIST PARTS NAME PRICE COLOR BY PRICE' \
  'LIST PARTS NAME WITH COLOR = blue' \
  'SELECT PARTS WITH COLOR = blue' \
  'COUNT PARTS' \
  'COUNT PARTS' \
  'LIST DICT PARTS' \
  'CT DICT PARTS PRICE' | tclrun)"

# record verbs + ED scripted session
check tcl-records "$(printf '%s\n' \
  'COPY PARTS W100 TO W900' \
  'CT PARTS W900' \
  'DELETE PARTS W900' \
  'ED PARTS NOTE1' \
  'I' \
  'alpha' \
  'beta' \
  '.' \
  'R/beta/BETA' \
  'FI' \
  'CT PARTS NOTE1' \
  'DELETE PARTS NOTE1' | tclrun)"

# privilege gate
check tcl-gate "$(printf '!echo leaked\n' | tclrun; \
  printf '!echo allowed\n' | MVXPRIV=unrestricted "$TCL" -a "$ACCT" 2>&1)"

# self-hosting: BASIC + CATALOG a verb, then run it.  BP is a directory
# file, so its dictionary carries a %FILE% marking it "dir".
mkdir -p "$ACCT/BP" "$ACCT/BP.DICT"
printf 'FILE\375dir\n' > "$ACCT/BP.DICT/%FILE%"
cat > "$ACCT/BP/HELLO" <<'EOF'
* /**
*  * @file HELLO
*  * @version 3.1
*  */
PRINT "hello from HELLO"
PRINT "sentence: ":SENTENCE()
EOF
check tcl-selfhost "$( \
  printf 'BASIC BP HELLO\n' | tclrun; \
  printf 'CATALOG BP HELLO\n' | MVXPRIV=developer "$TCL" -a "$ACCT" 2>&1; \
  printf 'HELLO A B\n' | tclrun)"

# docblocks through I-type dictionary items
printf 'I\nDOCTAG(file)\n\nFile\n10L\n' > "$ACCT/BP.DICT/FILE"
printf 'I\nDOCTAG(version)\n\nVersion\n8L\n' > "$ACCT/BP.DICT/VERSION"
check tcl-docblock "$(printf 'LIST BP FILE VERSION\n' | tclrun)"

# packages: build, link (dependency pulls cmd), GIT help, unlink rules
"$ROOT/scripts/mkpkg.sh" "$ROOT/packages/cmd" >/dev/null
"$ROOT/scripts/mkpkg.sh" "$ROOT/packages/git" >/dev/null
check tcl-packages "$(printf '%s\n' \
  "LINK-PKG $ROOT/packages/git" \
  'LIST-PKGS' \
  'GIT' \
  "UNLINK-PKG $ROOT/packages/cmd" \
  "UNLINK-PKG $ROOT/packages/git" \
  "UNLINK-PKG $ROOT/packages/cmd" | tclrun)"

# account round-trip: mvx-convert-acct --export snapshots a live account
# to the git directory form; a plain-git-style clone (no hash-file store)
# is then rebuilt into hash files by mvx-convert-acct.
CONV="$ROOT/build/bin/mvx-convert-acct"
RTS="$TESTROOT/rtsrc"
"$ROOT/scripts/mkaccount.sh" "$RTS" >/dev/null
rtseed="$TESTROOT/rtseed.b"
cat > "$rtseed" <<'EOF'
X = CREATEFILE("PARTS")
OPEN "PARTS" TO F ELSE STOP
WRITE "Widget":@AM:"999" ON F, "W100"
WRITE "Gadget":@AM:"450" ON F, "G200"
OPEN "DICT", "PARTS" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Name":@AM:"12L" ON D, "NAME"
OPEN "VOC" TO V ELSE STOP
WRITE "V":@AM:"CATALOG/FOO" ON V, "FOO"
PRINT "seeded"
EOF
"$MVX" "$rtseed" -o "$TESTROOT/rtseedbin" 2>/dev/null
(cd "$RTS" && MVXACCOUNT=. "$TESTROOT/rtseedbin") >/dev/null
MVX="$TCL" "$CONV" --export "$RTS" >/dev/null 2>&1
RTC="$TESTROOT/rtclone"
mkdir -p "$RTC"
(cd "$RTS" && tar cf - --exclude=mvxdata.lmdb .) | (cd "$RTC" && tar xf -)
MVX="$TCL" "$CONV" "$RTC" >/dev/null 2>&1
check tcl-account "$(printf '%s\n' \
  'COUNT PARTS' \
  'LIST PARTS NAME BY NAME' \
  'CT VOC FOO' | "$TCL" -a "$RTC" 2>&1 | normalise)"

# %FILE%-driven type: on import each file is rebuilt as the backend its
# dictionary's %FILE% names - a hash file for ORDERS, a directory file
# for ARCHIVE.
RDS="$TESTROOT/rdsrc"
"$ROOT/scripts/mkaccount.sh" "$RDS" >/dev/null
rdseed="$TESTROOT/rdseed.b"
cat > "$rdseed" <<'EOF'
X = CREATEFILE("ORDERS")
OPEN "ORDERS" TO F ELSE STOP
WRITE "a":@AM:"1" ON F, "O1"
WRITE "b":@AM:"2" ON F, "O2"
OPEN "DICT", "ORDERS" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Who":@AM:"10L" ON D, "WHO"
X = CREATEFILE("ARCHIVE", "DIR")
OPEN "ARCHIVE" TO A ELSE STOP
WRITE "old":@AM:"x" ON A, "R1"
PRINT "seeded"
EOF
"$MVX" "$rdseed" -o "$TESTROOT/rdseedbin" 2>/dev/null
(cd "$RDS" && MVXACCOUNT=. "$TESTROOT/rdseedbin") >/dev/null
MVX="$TCL" "$CONV" --export "$RDS" >/dev/null 2>&1
RDC="$TESTROOT/rdclone"
mkdir -p "$RDC"
# clone without the hash-file store
(cd "$RDS" && tar cf - --exclude=mvxdata.lmdb .) | (cd "$RDC" && tar xf -)
MVX="$TCL" "$CONV" "$RDC" >/dev/null 2>&1
check tcl-account-dict "$( \
  "$TCL" -a "$RDC" -c 'COUNT ORDERS' 2>&1; \
  "$TCL" -a "$RDC" -c 'CT DICT ORDERS WHO' 2>&1 | head -1; \
  "$TCL" -a "$RDC" -c 'COUNT ARCHIVE' 2>&1; \
  { [ -d "$RDC/ORDERS" ] && echo 'ORDERS still directory' || echo 'ORDERS is a hash file'; }; \
  { [ -d "$RDC/ARCHIVE" ] && echo 'ARCHIVE stays a directory file' || echo 'ARCHIVE lost'; })"

# CONVERT-FILE: change one file's backend, records + dictionary intact
CFA="$TESTROOT/cfacct"
"$ROOT/scripts/mkaccount.sh" "$CFA" >/dev/null
cfseed="$TESTROOT/cfseed.b"
cat > "$cfseed" <<'EOF'
X = CREATEFILE("PARTS")
OPEN "PARTS" TO F ELSE STOP
WRITE "Widget":@AM:"999" ON F, "W100"
WRITE "Gadget":@AM:"450" ON F, "G200"
OPEN "DICT", "PARTS" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Name":@AM:"12L" ON D, "NAME"
PRINT "seeded"
EOF
"$MVX" "$cfseed" -o "$TESTROOT/cfseedbin" 2>/dev/null
(cd "$CFA" && MVXACCOUNT=. "$TESTROOT/cfseedbin") >/dev/null
check tcl-convert-file "$( \
  "$TCL" -a "$CFA" -c 'CONVERT-FILE PARTS dir' 2>&1; \
  { [ -d "$CFA/PARTS" ] && echo 'PARTS is now a directory file' || echo 'not a dir'; }; \
  "$TCL" -a "$CFA" -c 'COUNT PARTS' 2>&1; \
  "$TCL" -a "$CFA" -c 'CONVERT-FILE PARTS lmdb' 2>&1; \
  { [ -d "$CFA/PARTS" ] && echo 'still a dir' || echo 'PARTS is a hash file again'; }; \
  "$TCL" -a "$CFA" -c 'LIST PARTS NAME BY NAME' 2>&1 | normalise)"

# mvx-git: the git-wrapper bin command (built by the git package) clones
# an account's legible form and rebuilds its hash files.  Needs the real
# git CLI and the built wrapper, so it is skipped where either is absent.
if command -v git >/dev/null 2>&1 && [ -x "$ROOT/build/bin/mvx-git" ]; then
  MGS="$TESTROOT/mgsrc"
  "$ROOT/scripts/mkaccount.sh" "$MGS" >/dev/null
  mgseed="$TESTROOT/mgseed.b"
  cat > "$mgseed" <<'EOF'
X = CREATEFILE("PARTS")
OPEN "PARTS" TO F ELSE STOP
WRITE "Widget":@AM:"999" ON F, "W100"
OPEN "DICT", "PARTS" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Name":@AM:"12L" ON D, "NAME"
PRINT "seeded"
EOF
  "$MVX" "$mgseed" -o "$TESTROOT/mgseedbin" 2>/dev/null
  (cd "$MGS" && MVXACCOUNT=. "$TESTROOT/mgseedbin") >/dev/null
  MVX="$TCL" "$CONV" --export "$MGS" >/dev/null 2>&1
  ( cd "$MGS" && printf 'mvxdata.lmdb/\n' > .gitignore && git init -q -b main && \
    git add -A && git -c user.email=t@t -c user.name=t commit -qm acct ) >/dev/null 2>&1
  MGC="$TESTROOT/mgclone"
  MVXCONVERT="$CONV" MVX="$TCL" "$ROOT/build/bin/mvx-git" clone "$MGS" "$MGC" >/dev/null 2>&1
  # a repo WITHOUT a .mvx descriptor: plain clone stays plain; only an
  # explicit opt-in ($MVXGIT_CREATE) turns it into a new account.
  MGP="$TESTROOT/mgplain"
  mkdir -p "$MGP/BP"; printf 'PRINT "hi"\n' > "$MGP/BP/HELLO"
  ( cd "$MGP" && git init -q -b main && git add -A && \
    git -c user.email=t@t -c user.name=t commit -qm src ) >/dev/null 2>&1
  MGPD="$TESTROOT/mgplain-default"
  MVXCONVERT="$CONV" MVX="$TCL" "$ROOT/build/bin/mvx-git" clone "$MGP" "$MGPD" </dev/null >/dev/null 2>&1
  MGPY="$TESTROOT/mgplain-optin"
  MVXGIT_CREATE=1 MVXCONVERT="$CONV" MVX="$TCL" "$ROOT/build/bin/mvx-git" clone "$MGP" "$MGPY" </dev/null >/dev/null 2>&1
  check tcl-mvxgit "$( \
    { [ -d "$MGC/mvxdata.lmdb" ] && echo 'account clone (.mvx) rebuilt' \
        || echo 'account clone NOT rebuilt'; }; \
    "$TCL" -a "$MGC" -c 'COUNT PARTS' 2>&1; \
    { [ -f "$MGPD/.mvx" ] && echo 'plain clone became account (WRONG)' \
        || echo 'plain clone stayed plain'; }; \
    { [ -f "$MGPY/.mvx" ] && echo 'opt-in clone created account' \
        || echo 'opt-in clone NOT created'; })"
else
  echo "  (skipping tcl-mvxgit: git CLI or build/bin/mvx-git unavailable)"
fi

# native git (libgit2, no shell, restricted tier): init, export, add,
# commit, log - the injection-proof structured path
GACCT="$TESTROOT/gitacct"
mkdir -p "$GACCT/CATALOG"
"$TCL" -a "$GACCT" -c "CREATE-FILE CUST" >/dev/null 2>&1
gseed="$TESTROOT/gseed.b"
cat > "$gseed" <<'GSEOF'
OPEN "CUST" TO F ELSE STOP
WRITE "Ada":@AM:"London" ON F, "C1"
WRITE "Bob":@AM:"Paris" ON F, "C2"
GSEOF
"$MVX" "$gseed" -o "$TESTROOT/gseedbin" 2>/dev/null
(cd "$GACCT" && MVXACCOUNT=. "$TESTROOT/gseedbin")
check tcl-gitnative "$( \
  printf "LINK-PKG $ROOT/packages/git\nGIT INIT\nGIT ADD CUST\nGIT STATUS\nGIT COMMIT -m initial\nGIT LOG\n" | \
    "$TCL" -a "$GACCT" 2>&1 | sed -E 's/[0-9a-f]{7,40}/HASH/g' | normalise; \
  printf 'DELETE CUST C1\nWRITE-C2\n' > /dev/null; \
  (cd "$GACCT" && MVXACCOUNT=. "$MVX" /dev/stdin -o "$TESTROOT/gmod" <<'GMEOF' >/dev/null 2>&1
OPEN "CUST" TO F ELSE STOP
WRITE "Bob":@AM:"Berlin" ON F, "C2"
GMEOF
   cd "$GACCT" && MVXACCOUNT=. "$TESTROOT/gmod"); \
  printf 'GIT STATUS\nGIT DIFF CUST\nGIT RESTORE CUST\nGIT STATUS\nCT CUST C2\n' | "$TCL" -a "$GACCT" 2>&1)"

# BUILD: provision an account from git-tracked config after a clone.
# A dictionary directory + BP source with no data file -> BUILD makes
# the file, imports the schema, catalogs the source.
VENDOR="$TESTROOT/vendor"
mkdir -p "$VENDOR/CATALOG" "$VENDOR/BP"
"$TCL" -a "$VENDOR" -c "CREATE-FILE PARTS" >/dev/null 2>&1
bseed="$TESTROOT/bseed.b"
cat > "$bseed" <<'BSEOF'
OPEN "DICT","PARTS" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Name":@AM:"20L" ON D, "NAME"
BSEOF
"$MVX" "$bseed" -o "$TESTROOT/bseedbin" 2>/dev/null
(cd "$VENDOR" && MVXACCOUNT=. "$TESTROOT/bseedbin")
cat > "$VENDOR/BP/RPT" <<'RPTEOF'
/**
 * @file RPT
 */
PRINT "report verb works"
RPTEOF
MVXPRIV=developer "$TCL" -a "$VENDOR" -c "EXPORT DICT PARTS" >/dev/null 2>&1
printf 'PARTS lmdb\n' > "$VENDOR/FILES"
# "clone": copy only the tracked config, not the data
CLONE="$TESTROOT/clone"
mkdir -p "$CLONE"
cp -r "$VENDOR/PARTS.DICT" "$VENDOR/BP" "$VENDOR/FILES" "$CLONE/"
check tcl-build "$( \
  MVXPRIV=developer "$TCL" -a "$CLONE" -c "BUILD" 2>&1 | normalise; \
  printf 'LISTF\nLIST DICT PARTS\nRPT\n' | "$TCL" -a "$CLONE" 2>&1 | normalise)"

# delivery: stock -> branch(site) -> customise -> cherry-pick upstream
# -> merge stock down.  The Pick multi-site delivery workflow in git.
DACCT="$TESTROOT/delacct"
mkdir -p "$DACCT/CATALOG"
"$TCL" -a "$DACCT" -c "CREATE-FILE MENU" >/dev/null 2>&1
dseed2="$TESTROOT/dseed2.b"
cat > "$dseed2" <<'DEOF'
OPEN "MENU" TO F ELSE STOP
WRITE "Sales" ON F, "M1"
DEOF
"$MVX" "$dseed2" -o "$TESTROOT/dseed2bin" 2>/dev/null
(cd "$DACCT" && MVXACCOUNT=. "$TESTROOT/dseed2bin")
custom="$TESTROOT/dcustom.b"
cat > "$custom" <<'DCEOF'
OPEN "MENU" TO F ELSE STOP
WRITE "ACME Dashboard" ON F, "M9"
DCEOF
"$MVX" "$custom" -o "$TESTROOT/dcustombin" 2>/dev/null
check tcl-delivery "$( \
  export MVXACCOUNT="$DACCT"; \
  { printf "LINK-PKG $ROOT/packages/git\nGIT INIT\nGIT ADD MENU\nGIT COMMIT -m stock\nGIT BRANCH site\nGIT CHECKOUT site\n" | "$TCL" -a "$DACCT" 2>&1; \
    (cd "$DACCT" && "$TESTROOT/dcustombin"); \
    printf 'GIT ADD MENU\nGIT COMMIT -m acme-custom\nGIT CHECKOUT main\nGIT CHERRY-PICK site\nCT MENU M9\nGIT BRANCH\n' | "$TCL" -a "$DACCT" 2>&1; \
  } | sed -E 's/\[[0-9a-f]{7,40}\]/[HASH]/g; s/^[0-9a-f]{7,40} /HASH /g' | normalise; \
  unset MVXACCOUNT)"

# GITIGNORE: bulk data excluded, dictionary tracked
IGACCT="$TESTROOT/igacct"
mkdir -p "$IGACCT/CATALOG"
"$TCL" -a "$IGACCT" -c "CREATE-FILE ORDERS" >/dev/null 2>&1
igseed="$TESTROOT/igseed.b"
cat > "$igseed" <<'IGEOF'
OPEN "ORDERS" TO F ELSE STOP
FOR I = 1 TO 5
   WRITE "order-":I ON F, "O":I
NEXT I
OPEN "DICT", "ORDERS" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Customer":@AM:"20L" ON D, "CUST"
IGEOF
"$MVX" "$igseed" -o "$TESTROOT/igseedbin" 2>/dev/null
(cd "$IGACCT" && MVXACCOUNT=. "$TESTROOT/igseedbin")
check tcl-gitignore "$(printf '%s\n' \
  "LINK-PKG $ROOT/packages/git" \
  'GIT INIT' \
  'GIT IGNORE ORDERS' \
  'GIT ADD ORDERS' \
  'GIT ADD DICT ORDERS' \
  'GIT STATUS' | "$TCL" -a "$IGACCT" 2>&1 | normalise)"

# PORT-SOURCE: C-style comments to classic, output must compile
cat > "$ACCT/BP/CPORT" <<'EOF'
/**
 * @file CPORT
 */
// setup
A = 5 /* five */ + 1
B = "keep /* this */"   // trailing
/* block
   spans lines */
PRINT A:" ":B
EOF
check tcl-port "$(printf 'PORT-SOURCE BP CPORT\nCT BP CPORT.PORTED\n' | tclrun; \
  printf 'BASIC BP CPORT.PORTED\n' | MVXPRIV=developer "$TCL" -a "$ACCT" 2>&1)"

# secondary indexes: build, query through them, write-path maintenance
check tcl-index "$(printf '%s\n' \
  'CREATE-INDEX PARTS COLOR' \
  'LIST-INDEXES PARTS' \
  'LIST PARTS NAME WITH COLOR = blue' \
  'COPY PARTS W100 TO W950' \
  'LIST PARTS NAME WITH COLOR = blue' \
  'DELETE PARTS W950' \
  'LIST PARTS NAME WITH COLOR = blue' \
  'ED PARTS G200' \
  '3' \
  'R/red/blue' \
  'FI' \
  'LIST PARTS NAME WITH COLOR = blue' \
  'ED PARTS G200' \
  '3' \
  'R/blue/red' \
  'FI' \
  'LIST PARTS NAME WITH COLOR = red' \
  'CREATE-INDEX PARTS NAME' \
  'DELETE-INDEX PARTS COLOR' \
  'LIST-INDEXES PARTS' \
  'LIST PARTS NAME WITH COLOR = blue' \
  'DELETE-INDEX PARTS NAME' | tclrun)"

# EXPORT/IMPORT: a hash file round-trips through a git-native
# directory file; an external edit and a delete both mirror back
exacct_prog="$TESTROOT/exseed.b"
cat > "$exacct_prog" <<'EXEOF'
OPEN "CUST" TO F ELSE STOP
WRITE "Ada":@AM:"London" ON F, "C1"
WRITE "Bob":@AM:"Paris" ON F, "C2"
WRITE "Cy":@AM:"Berlin" ON F, "C3"
EXEOF
"$MVX" "$exacct_prog" -o "$TESTROOT/exseed" 2>/dev/null
check tcl-export "$( \
  printf 'CREATE-FILE CUST\n' | tclrun; \
  (cd "$ACCT" && MVXACCOUNT=. "$TESTROOT/exseed"); \
  printf 'EXPORT CUST\n' | tclrun; \
  printf 'Ada\nLONDON\n' > "$ACCT/CUST.EXP/C1"; \
  rm -f "$ACCT/CUST.EXP/C2"; \
  printf 'IMPORT CUST\nLIST CUST\nCT CUST C1\n' | tclrun)"

# VI: export a record to a file, "edit" it with a scripted editor,
# import it back — the hash-file <-> text-file round trip
fakeed="$TESTROOT/fakeed.sh"
cat > "$fakeed" <<'FEEOF'
#!/bin/sh
awk 'NR==2{print toupper($0);next}{print}' "$1" > "$1.t" && mv "$1.t" "$1"
FEEOF
chmod +x "$fakeed"
check tcl-vi "$( \
  printf 'CREATE-FILE NOTES\n' | tclrun; \
  (cd "$ACCT" && MVXACCOUNT=. "$MVX" /dev/stdin -o "$TESTROOT/ns" <<'NSEOF' >/dev/null 2>&1
OPEN "NOTES" TO F ELSE STOP
WRITE "line one":@AM:"line two":@AM:"line three" ON F, "N1"
NSEOF
   cd "$ACCT" && MVXACCOUNT=. "$TESTROOT/ns"); \
  printf 'VI NOTES N1\n' | tclrun; \
  MVXPRIV=unrestricted MVXEDITOR="$fakeed" "$TCL" -a "$ACCT" -c "VI NOTES N1" 2>&1; \
  printf 'CT NOTES N1\n' | tclrun; \
  MVXPRIV=unrestricted MVXEDITOR=true "$TCL" -a "$ACCT" -c "VI NOTES N1" 2>&1)"

# select lists crossing EXECUTE into a program
prog="$TESTROOT/progsel.b"
cat > "$prog" <<'EOF'
EXECUTE "SELECT PARTS WITH COLOR = blue" CAPTURING X
DONE = 0
LOOP
   READNEXT ID ELSE DONE = 1
UNTIL DONE DO
   PRINT "got ":ID
REPEAT
EOF
"$MVX" "$prog" -o "$TESTROOT/progsel" 2>/dev/null
check tcl-session "$(cd "$ACCT" && \
  MVXACCOUNT=. MVXSESSION="$TESTROOT/sess" "$TESTROOT/progsel" 2>&1)"

# the networked daemon: same account flow through mvx-lmdbd, plus the lock
# lease (holder dies without releasing; next session proceeds)
DSOCK="/tmp/mvx-lmdbd-test-$$.sock"
DACCT="$TESTROOT/dacct"
mkdir -p "$DACCT"
"$ROOT/build/bin/mvx-lmdbd" -d "$TESTROOT/ddata" -s "$DSOCK" 2>/dev/null &
DPID=$!
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
  [ -S "$DSOCK" ] && break
  sleep 0.1
done
dlock="$TESTROOT/dlock.b"
cat > "$dlock" <<'EOF'
OPEN "PARTS" TO F ELSE STOP
READU R FROM F, "W100" THEN PRINT "locked, exiting without RELEASE"
EOF
"$MVX" "$dlock" -o "$TESTROOT/dlockbin" 2>/dev/null
check tcl-daemon "$( \
  export MVXDAEMON="$DSOCK"; \
  "$TCL" -a "$DACCT" -c "CREATE-FILE VOC" >/dev/null 2>&1; \
  printf 'CREATE-FILE PARTS\n' | "$TCL" -a "$DACCT" 2>&1; \
  (cd "$DACCT" && MVXACCOUNT=. MVXDAEMON="$DSOCK" "$TESTROOT/seedbin"); \
  printf 'LIST PARTS NAME COLOR\nCREATE-INDEX PARTS COLOR\nLIST PARTS NAME WITH COLOR = blue\nLISTF\n' | \
    "$TCL" -a "$DACCT" 2>&1; \
  (cd "$DACCT" && MVXACCOUNT=. MVXDAEMON="$DSOCK" "$TESTROOT/dlockbin"); \
  (cd "$DACCT" && MVXACCOUNT=. MVXDAEMON="$DSOCK" "$TESTROOT/dlockbin"); \
  unset MVXDAEMON)"
# mixed local/remote: per-file REMOTE binding with an explicit daemon
# address and NO $MVXDAEMON - locals stay local, the bound file goes
# through the daemon, one program reads both
MACCT="$TESTROOT/mixacct"
mkdir -p "$MACCT"
"$TCL" -a "$MACCT" -c "CREATE-FILE VOC" >/dev/null 2>&1
mixprog="$TESTROOT/mix.b"
cat > "$mixprog" <<'MIXEOF'
OPEN "LOCALF" TO L ELSE STOP
WRITE "local data" ON L, "L1"
OPEN "SHARED" TO R ELSE STOP
WRITE "remote data" ON R, "R1"
READ A FROM L, "L1" THEN PRINT "local read: ":A
READ B FROM R, "R1" THEN PRINT "remote read: ":B
MIXEOF
"$MVX" "$mixprog" -o "$TESTROOT/mixbin" 2>/dev/null
check tcl-mixed "$( \
  printf "CREATE-FILE LOCALF\nCREATE-FILE SHARED USING lmdbnet $DSOCK\nLISTF\n" | \
    "$TCL" -a "$MACCT" 2>&1 | sed "s#$DSOCK#@DSOCK@#g"; \
  (cd "$MACCT" && MVXACCOUNT=. "$TESTROOT/mixbin"); \
  printf 'LISTF\n' | "$TCL" -a "$MACCT" 2>&1)"

kill $DPID 2>/dev/null
rm -f "$DSOCK"

# ---------------------------------------------------------------- phase 3
if [ "$QUICK" = 0 ]; then
  echo "== sieve"
  "$MVX" "$ROOT/bench/sieve.b" -o "$TESTROOT/sieve" 2>/dev/null
  sieve_out="$("$TESTROOT/sieve")"
  if printf '%s' "$sieve_out" | grep -q "Count: 78498" &&
     printf '%s' "$sieve_out" | grep -q "VALID"; then
    PASS=$((PASS + 1))
    echo "  sieve valid ($(printf '%s' "$sieve_out" | head -1))"
  else
    echo "FAIL sieve: $sieve_out"
    FAIL=$((FAIL + 1))
  fi
fi

# -------------------------------------------------------- phase 4: install
# Install to a throwaway prefix and drive it with every MVX_* override
# unset, proving the binaries locate the runtime, drivers, and system
# account relative to themselves.
if [ "$QUICK" = 0 ]; then
  echo "== install"
  IPFX="$TESTROOT/prefix"
  rm -rf "$IPFX"
  if cmake --install "$ROOT/build" --prefix "$IPFX" >/dev/null 2>&1; then
    IACCT="$TESTROOT/iacct"
    rm -rf "$IACCT"; mkdir -p "$IACCT"
    prog="$IACCT/hi.b"
    printf 'OPEN "PARTS" TO F ELSE STOP\nWRITE "Widget":@AM:"9.99" ON F,"W1"\nREAD R FROM F,"W1" THEN PRINT "installed:":R<1>\n' > "$prog"
    out="$(
      env -u MVXSYSTEM -u MVXDRIVERS -u MVXBIN -u MVXSESSION -u MVXACCOUNT \
          -u DYLD_LIBRARY_PATH -u LD_LIBRARY_PATH sh -c '
        P="$1"; A="$2"; PR="$3"
        "$P/bin/mvx" -a "$A" -c "CREATE-FILE VOC"   >/dev/null 2>&1
        "$P/bin/mvx" -a "$A" -c "CREATE-FILE PARTS" >/dev/null 2>&1
        "$P/bin/mvx-basic" "$PR" -o "$A/hi" 2>&1 || { echo COMPILE-FAIL; exit 0; }
        (cd "$A" && MVXACCOUNT=. ./hi)
      ' _ "$IPFX" "$IACCT" "$prog" 2>&1)"
    if [ "$out" = "installed:Widget" ]; then
      PASS=$((PASS + 1)); echo "  install ok (self-locating, no MVX_* vars)"
    else
      FAIL=$((FAIL + 1)); echo "FAIL install: $out"
    fi
  else
    FAIL=$((FAIL + 1)); echo "FAIL install: cmake --install failed"
  fi
fi

echo "== $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
