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
  # Substitute absolute roots, then collapse the column padding that
  # trails a substituted path.  A verb that FMT-pads a path column (e.g.
  # LIST-PKGS) sizes the padding from the *absolute* path length, which
  # differs by platform; @ROOT@ hides the path but not the trailing
  # spaces, so squeeze 2+ spaces after a normalised path token to one.
  sed -E -e "s#$TESTROOT#@TESTROOT@#g" -e "s#$ROOT#@ROOT@#g" \
         -e "s#(@(TEST)?ROOT@[^ ]*)  +#\1 #g"
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
lang ongoto

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

# user-defined FUNCTIONs (DEFFUN + RETURN(value)), linked from many sources
out="$TESTROOT/funcs"
if "$MVX" "$ROOT/tests/funcs.b" "$ROOT/tests/func_square.b" \
     "$ROOT/tests/func_fact.b" "$ROOT/tests/func_greet.b" -o "$out" \
     2>"$TESTROOT/cerr"; then
  check funcs "$("$out" 2>&1)"
else
  check funcs "COMPILE FAILED: $(cat "$TESTROOT/cerr")"
fi

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

# SORT (LIST sorted by id by default, or BY key) and SSELECT (a sorted
# select list feeding the next command)
check tcl-sort "$(printf '%s\n' \
  'SORT PARTS NAME PRICE COLOR' \
  'SORT PARTS NAME PRICE BY PRICE' \
  'SSELECT PARTS' \
  'LIST PARTS NAME' \
  'SSELECT PARTS WITH COLOR = blue' \
  'LIST PARTS NAME' | tclrun)"

# multivalue explosion + dictionary associations (D-item attr 6): an
# ORDERS file whose PRODUCT/QTY/PRICE are a parallel-multivalue group.
# O1 has two line items, O2 one; the associated columns explode onto
# aligned sub-rows while the single-valued Customer shows once.
oseed="$TESTROOT/oseed.b"
cat > "$oseed" <<'EOF'
X = CREATEFILE("ORDERS")
OPEN "ORDERS" TO F ELSE STOP
R = ""
R<1> = "Acme Corp"
R<5> = "Widget":@VM:"Gadget"
R<6> = "2":@VM:"1"
R<7> = "999":@VM:"450"
WRITE R ON F, "O1"
R = ""
R<1> = "Beta Ltd"
R<5> = "Sprocket"
R<6> = "5"
R<7> = "125"
WRITE R ON F, "O2"
OPEN "DICT", "ORDERS" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Customer":@AM:"12L" ON D, "CUSTOMER"
WRITE "D":@AM:"5":@AM:"":@AM:"Product":@AM:"10L":@AM:"ORDERITEMS" ON D, "PRODUCT"
WRITE "D":@AM:"6":@AM:"":@AM:"Qty":@AM:"5R":@AM:"ORDERITEMS" ON D, "QTY"
WRITE "D":@AM:"7":@AM:"MD2$":@AM:"Price":@AM:"8R":@AM:"ORDERITEMS" ON D, "PRICE"
PRINT "seeded"
EOF
"$MVX" "$oseed" -o "$TESTROOT/oseedbin" 2>/dev/null
(cd "$ACCT" && MVXACCOUNT=. "$TESTROOT/oseedbin") >/dev/null

check tcl-assoc "$(printf '%s\n' \
  'LIST ORDERS CUSTOMER PRODUCT QTY PRICE' \
  'SORT ORDERS CUSTOMER PRODUCT QTY PRICE' | tclrun)"

# SQL mapping (#18 phase 1): the dictionary -> relational schema. Single
# attrs become parent columns; the ORDERITEMS association a child table.
# First a selective map (QTY omitted), then map-all with the data preview.
check tcl-map "$(printf '%s\n' \
  'MAP ORDERS CUSTOMER PRODUCT PRICE' \
  'MAP ORDERS DATA' | tclrun)"

# account credential store (.mvx-private): set/list with values masked,
# and upsert replacing an existing entry in place
check tcl-cred "$(printf '%s\n' \
  'SET-CREDENTIAL lmdbnet mvxdb-a:4300 SALES token=abc123' \
  'SET-CREDENTIAL postgres db:5432 mvx user=app password=s3cret' \
  'LIST-CREDENTIALS' \
  'SET-CREDENTIAL lmdbnet mvxdb-a:4300 SALES token=NEWTOK' \
  'LIST-CREDENTIALS' | tclrun)"

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

# namespace isolation: two accounts (nsa, nsb) with different names share
# ONE daemon; the same file name ORDERS holds different data in each and
# is mutually invisible.  A third account (nsc) names nsa's namespace
# explicitly in BINDINGS and reads nsa's data — the Q-pointer analog.
NSA="$TESTROOT/nsa"; NSB="$TESTROOT/nsb"; NSC="$TESTROOT/nsc"
mkdir -p "$NSA" "$NSB" "$NSC"
printf 'OPEN "ORDERS" TO F ELSE STOP\nWRITE "from A" ON F, "O1"\n' > "$TESTROOT/wa.b"
printf 'OPEN "ORDERS" TO F ELSE STOP\nWRITE "from B" ON F, "O1"\n' > "$TESTROOT/wb.b"
printf 'OPEN "ORDERS" TO F ELSE STOP\nREAD V FROM F, "O1" THEN PRINT V ELSE PRINT "(none)"\n' > "$TESTROOT/rd.b"
"$MVX" "$TESTROOT/wa.b" -o "$TESTROOT/wa" 2>/dev/null
"$MVX" "$TESTROOT/wb.b" -o "$TESTROOT/wb" 2>/dev/null
"$MVX" "$TESTROOT/rd.b" -o "$TESTROOT/rd" 2>/dev/null
check tcl-namespace "$( \
  export MVXDAEMON="$DSOCK"; \
  "$TCL" -a "$NSA" -c "CREATE-FILE VOC" >/dev/null 2>&1; \
  "$TCL" -a "$NSA" -c "CREATE-FILE ORDERS" >/dev/null 2>&1; \
  "$TCL" -a "$NSB" -c "CREATE-FILE VOC" >/dev/null 2>&1; \
  "$TCL" -a "$NSB" -c "CREATE-FILE ORDERS" >/dev/null 2>&1; \
  (cd "$NSA" && MVXACCOUNT=. "$TESTROOT/wa"); \
  (cd "$NSB" && MVXACCOUNT=. "$TESTROOT/wb"); \
  printf 'A reads: '; (cd "$NSA" && MVXACCOUNT=. "$TESTROOT/rd"); \
  printf 'B reads: '; (cd "$NSB" && MVXACCOUNT=. "$TESTROOT/rd"); \
  unset MVXDAEMON; \
  printf 'ORDERS lmdbnet %s nsa\n' "$DSOCK" > "$NSC/BINDINGS"; \
  printf 'C via nsa: '; (cd "$NSC" && MVXACCOUNT=. "$TESTROOT/rd") )"

kill $DPID 2>/dev/null
rm -f "$DSOCK"

# daemon authentication: mvx-lmdbd-admin provisions a namespace token
# (offline, into <datadir>/accounts); a client with the token in
# .mvx-private reads/writes, a client with the wrong token is denied.
ADATA="$TESTROOT/adata"; ASOCK="/tmp/mvx-auth-test-$$.sock"
AACCT="$TESTROOT/aacct"; BACCT="$TESTROOT/bacct"
mkdir -p "$AACCT/.mvx-private" "$BACCT/.mvx-private"
chmod 700 "$AACCT/.mvx-private" "$BACCT/.mvx-private"
ATOK="$("$ROOT/build/bin/mvx-lmdbd-admin" -d "$ADATA" create-account acct1 2>/dev/null)"
"$ROOT/build/bin/mvx-lmdbd" -d "$ADATA" -s "$ASOCK" 2>/dev/null &
APID=$!
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
  [ -S "$ASOCK" ] && break
  sleep 0.1
done
printf 'lmdbnet %s acct1 token=%s\n' "$ASOCK" "$ATOK" > "$AACCT/.mvx-private/credentials"
printf 'ORDERS lmdbnet %s acct1\n' "$ASOCK" > "$AACCT/BINDINGS"
printf 'lmdbnet %s acct1 token=deadbeefwrong\n' "$ASOCK" > "$BACCT/.mvx-private/credentials"
printf 'ORDERS lmdbnet %s acct1\n' "$ASOCK" > "$BACCT/BINDINGS"
chmod 600 "$AACCT/.mvx-private/credentials" "$BACCT/.mvx-private/credentials"
printf 'OPEN "ORDERS" TO F ELSE STOP\nWRITE "hi" ON F, "O1"\nREAD V FROM F, "O1" THEN PRINT "read: ":V\n' > "$TESTROOT/wauth.b"
printf 'OPEN "ORDERS" TO F ELSE PRINT "denied"\n' > "$TESTROOT/rauth.b"
"$MVX" "$TESTROOT/wauth.b" -o "$TESTROOT/wauth" 2>/dev/null
"$MVX" "$TESTROOT/rauth.b" -o "$TESTROOT/rauth" 2>/dev/null
check tcl-auth "$( \
  "$TCL" -a "$AACCT" -c "CREATE-FILE ORDERS" 2>&1 | sed "s#$ASOCK#@SOCK@#g"; \
  (cd "$AACCT" && MVXACCOUNT=. "$TESTROOT/wauth"); \
  printf 'wrong token: '; (cd "$BACCT" && MVXACCOUNT=. "$TESTROOT/rauth" 2>/dev/null); \
  "$ROOT/build/bin/mvx-lmdbd-admin" -d "$ADATA" list-accounts)"

# named connection profiles: BINDINGS references @conn1, and the host +
# namespace + token live in the local connection profile — so the
# committed binding never names the daemon.
CTOK="$("$ROOT/build/bin/mvx-lmdbd-admin" -d "$ADATA" create-account connns 2>/dev/null)"
CACCT="$TESTROOT/cacct"; mkdir -p "$CACCT"
check tcl-conn "$( \
  printf 'SET-CONNECTION conn1 driver=lmdbnet address=%s namespace=connns token=%s\nLIST-CONNECTIONS\nCREATE-FILE ORDERS USING @conn1\n' \
    "$ASOCK" "$CTOK" | "$TCL" -a "$CACCT" 2>&1 | sed "s#$ASOCK#@SOCK@#g"; \
  printf 'BINDINGS: '; cat "$CACCT/BINDINGS"; \
  (cd "$CACCT" && MVXACCOUNT=. "$TESTROOT/wauth"))"
kill $APID 2>/dev/null
rm -f "$ASOCK"

# postgres backend — only when MVX_PG names a reachable database, e.g.
#   MVX_PG='address=localhost:5432 dbname=mvx user=mvx password=mvx'
# Records round-trip byte-exact through a table (id/rec BYTEA), bound
# through a @connection; the schema isolates the namespace.
if [ -n "${MVX_PG:-}" ]; then
  # psql helper for the native-read test's "external writer" — parse the
  # connection out of MVX_PG (address=host:port dbname=.. user=.. password=..)
  PG_ADDR=$(printf '%s\n' "$MVX_PG" | sed -n 's/.*address=\([^ ]*\).*/\1/p')
  PG_DB=$(printf '%s\n'   "$MVX_PG" | sed -n 's/.*dbname=\([^ ]*\).*/\1/p')
  PG_USER=$(printf '%s\n' "$MVX_PG" | sed -n 's/.*user=\([^ ]*\).*/\1/p')
  PG_PASS=$(printf '%s\n' "$MVX_PG" | sed -n 's/.*password=\([^ ]*\).*/\1/p')
  PG_HOST=${PG_ADDR%%:*}; PG_PORT=${PG_ADDR##*:}
  psql_ext() { PGPASSWORD="$PG_PASS" psql -h "$PG_HOST" -p "$PG_PORT" \
                 -U "$PG_USER" -d "$PG_DB" -qtAc "$1"; }

  PGACCT="$TESTROOT/pgacct"; mkdir -p "$PGACCT"
  printf 'SET-CONNECTION pgtest driver=postgres %s namespace=mvxtest\n' \
    "$MVX_PG" | "$TCL" -a "$PGACCT" >/dev/null 2>&1
  # drop any leftover table from a prior run: bind manually so DELETE-FILE
  # resolves to postgres, then start clean (the check re-creates it)
  printf 'ORDERS @pgtest\n' > "$PGACCT/BINDINGS"
  "$TCL" -a "$PGACCT" -c 'DELETE-FILE ORDERS' >/dev/null 2>&1
  printf 'OPEN "ORDERS" TO F ELSE STOP\nWRITE "Widget":@VM:"Gadget" ON F, "O1"\nWRITE "Acme" ON F, "O2"\nREAD V FROM F, "O1" THEN PRINT "read: ":V<1,1>:"/":V<1,2>\n' > "$TESTROOT/pg.b"
  "$MVX" "$TESTROOT/pg.b" -o "$TESTROOT/pgbin" 2>/dev/null
  check tcl-pg "$( \
    "$TCL" -a "$PGACCT" -c 'CREATE-FILE ORDERS USING @pgtest' 2>&1; \
    (cd "$PGACCT" && MVXACCOUNT=. "$TESTROOT/pgbin"); \
    printf 'COUNT ORDERS\nSELECT ORDERS\nLIST ORDERS\n' | \
      "$TCL" -a "$PGACCT" 2>&1)"

  # mapping phase 2 (#23/#26): BUILD-MAP projects single-valued attrs into
  # columns on the record's table and each association into a child table.
  printf 'MORD @pgtest\n' >> "$PGACCT/BINDINGS"
  "$TCL" -a "$PGACCT" -c 'DELETE-FILE MORD' >/dev/null 2>&1
  "$TCL" -a "$PGACCT" -c 'CREATE-FILE MORD USING @pgtest' >/dev/null 2>&1
  cat > "$TESTROOT/pgmap.b" <<'MEOF'
OPEN "MORD" TO F ELSE STOP
R = ""
R<1> = "Acme Corp"
R<5> = "Widget":@VM:"Gadget"
R<6> = "2":@VM:"1"
R<7> = "999":@VM:"450"
WRITE R ON F, "O1"
R = ""
R<1> = "Beta Ltd"
R<5> = "Sprocket"
R<6> = "5"
R<7> = "125"
WRITE R ON F, "O2"
OPEN "DICT", "MORD" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Customer":@AM:"20L" ON D, "CUSTOMER"
WRITE "D":@AM:"5":@AM:"":@AM:"Product":@AM:"10L":@AM:"ORDERITEMS" ON D, "PRODUCT"
WRITE "D":@AM:"6":@AM:"":@AM:"Qty":@AM:"5R":@AM:"ORDERITEMS" ON D, "QTY"
WRITE "D":@AM:"7":@AM:"MD2$":@AM:"Price":@AM:"8R":@AM:"ORDERITEMS" ON D, "PRICE"
MEOF
  "$MVX" "$TESTROOT/pgmap.b" -o "$TESTROOT/pgmapbin" 2>/dev/null
  (cd "$PGACCT" && MVXACCOUNT=. "$TESTROOT/pgmapbin")
  check tcl-mapbuild "$(printf 'BUILD-MAP MORD CUSTOMER PRODUCT QTY PRICE\n' | \
    "$TCL" -a "$PGACCT" 2>&1)"

  # mirror-on-write (#18): CREATE-MAP declares %MAP%; a later WRITE from a
  # program then auto-projects into the mapping (via the runtime hook).
  cat > "$TESTROOT/pgw3.b" <<'W3EOF'
OPEN "MORD" TO F ELSE STOP
R = ""
R<1> = "Gamma Inc"
R<5> = "Nut":@VM:"Washer"
R<6> = "40":@VM:"40"
R<7> = "0.05":@VM:"0.02"
WRITE R ON F, "O3"
PRINT "wrote O3"
W3EOF
  "$MVX" "$TESTROOT/pgw3.b" -o "$TESTROOT/pgw3bin" 2>/dev/null
  check tcl-mapmirror "$( \
    printf 'CREATE-MAP MORD CUSTOMER PRODUCT QTY PRICE\n' | \
      "$TCL" -a "$PGACCT" 2>&1; \
    (cd "$PGACCT" && MVXACCOUNT=. "$TESTROOT/pgw3bin"))"

  # LIST-MAPS / DELETE-MAP (#31), in a fresh account so LIST-MAPS is clean
  VMACCT="$TESTROOT/vmacct"; mkdir -p "$VMACCT"
  printf 'SET-CONNECTION vpg driver=postgres %s namespace=vmtest\n' \
    "$MVX_PG" | "$TCL" -a "$VMACCT" >/dev/null 2>&1
  printf 'PARTS @vpg\n' > "$VMACCT/BINDINGS"
  "$TCL" -a "$VMACCT" -c 'DELETE-FILE PARTS' >/dev/null 2>&1
  "$TCL" -a "$VMACCT" -c 'CREATE-FILE PARTS USING @vpg' >/dev/null 2>&1
  cat > "$TESTROOT/vm.b" <<'VMEOF'
OPEN "PARTS" TO F ELSE STOP
WRITE "Widget":@AM:"999" ON F, "P1"
OPEN "DICT", "PARTS" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Name":@AM:"12L" ON D, "NAME"
WRITE "D":@AM:"2":@AM:"MD2":@AM:"Price":@AM:"8R" ON D, "PRICE"
VMEOF
  "$MVX" "$TESTROOT/vm.b" -o "$TESTROOT/vmbin" 2>/dev/null
  (cd "$VMACCT" && MVXACCOUNT=. "$TESTROOT/vmbin")
  check tcl-mapverbs "$(printf '%s\n' \
    'CREATE-MAP PARTS NAME PRICE' \
    'LIST-MAPS' \
    'DELETE-MAP PARTS' \
    'LIST-MAPS' | "$TCL" -a "$VMACCT" 2>&1)"

  # typed DATE/TIME columns (#32): a D/MT dict item projects to a real
  # SQL date/time column carrying the internal value as ISO-8601, not the
  # locale-shaped display conversion.  Empty cells store NULL.
  printf 'EVT @vpg\n' >> "$VMACCT/BINDINGS"
  "$TCL" -a "$VMACCT" -c 'DELETE-FILE EVT' >/dev/null 2>&1
  "$TCL" -a "$VMACCT" -c 'CREATE-FILE EVT USING @vpg' >/dev/null 2>&1
  cat > "$TESTROOT/vmdt.b" <<'DTEOF'
OPEN "EVT" TO F ELSE STOP
R = ""
R<1> = "Launch"
R<2> = ICONV("25 JUL 2026", "D")
R<3> = ICONV("14:30:00", "MTS")
WRITE R ON F, "E1"
R = ""
R<1> = "Empty"
WRITE R ON F, "E2"
OPEN "DICT", "EVT" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Name":@AM:"12L" ON D, "NAME"
WRITE "D":@AM:"2":@AM:"D4/":@AM:"When":@AM:"12R" ON D, "WHEN"
WRITE "D":@AM:"3":@AM:"MTS":@AM:"At":@AM:"10R" ON D, "AT"
DTEOF
  "$MVX" "$TESTROOT/vmdt.b" -o "$TESTROOT/vmdtbin" 2>/dev/null
  (cd "$VMACCT" && MVXACCOUNT=. "$TESTROOT/vmdtbin")
  # LIST reads the record through the D/MT conversions; the SQL projection
  # rendered the same instants as ISO date/time columns (verified via psql).
  check tcl-mapdt "$(printf '%s\n' \
    'CREATE-MAP EVT NAME WHEN AT' \
    'SORT EVT BY NAME WHEN AT NAME' | "$TCL" -a "$VMACCT" 2>&1)"

  # native mode (#33): the typed columns are authoritative, so a WRITE whose
  # value does not fit its column is rejected (ON ERROR) and the record is
  # not written — versus mirror mode, which stores NULL and proceeds.
  # MAP-MODE views/sets the policy, refusing a switch that existing data
  # would violate.
  printf 'ITM @vpg\n' >> "$VMACCT/BINDINGS"
  "$TCL" -a "$VMACCT" -c 'DELETE-FILE ITM' >/dev/null 2>&1
  "$TCL" -a "$VMACCT" -c 'CREATE-FILE ITM USING @vpg' >/dev/null 2>&1
  cat > "$TESTROOT/vmi.b" <<'IEOF'
OPEN "ITM" TO F ELSE STOP
WRITE "Widget":@AM:"1000" ON F, "I1"
OPEN "DICT", "ITM" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Name":@AM:"12L" ON D, "NAME"
WRITE "D":@AM:"2":@AM:"MD2":@AM:"Price":@AM:"8R" ON D, "PRICE"
IEOF
  "$MVX" "$TESTROOT/vmi.b" -o "$TESTROOT/vmibin" 2>/dev/null
  (cd "$VMACCT" && MVXACCOUNT=. "$TESTROOT/vmibin")
  # native write: good row commits, bad (non-numeric price) row rejected
  cat > "$TESTROOT/vmnat.b" <<'NEOF'
OPEN "ITM" TO F ELSE STOP
WRITE "Gadget":@AM:"2550" ON F, "I2" ON ERROR PRINT "I2 rejected"
PRINT "I2 ok"
WRITE "Broken":@AM:"abc" ON F, "I3" ON ERROR PRINT "I3 rejected"
PRINT "done"
NEOF
  "$MVX" "$TESTROOT/vmnat.b" -o "$TESTROOT/vmnatbin" 2>/dev/null
  # mirror write: the same bad row is tolerated (projects NULL)
  cat > "$TESTROOT/vmmir.b" <<'MEOF'
OPEN "ITM" TO F ELSE STOP
WRITE "Junk":@AM:"notanum" ON F, "I9" ON ERROR PRINT "I9 rejected"
PRINT "I9 written"
MEOF
  "$MVX" "$TESTROOT/vmmir.b" -o "$TESTROOT/vmmirbin" 2>/dev/null
  check tcl-mapnative "$( \
    printf '%s\n' 'CREATE-MAP ITM NAME PRICE' 'MAP-MODE ITM native' \
      | "$TCL" -a "$VMACCT" 2>&1; \
    (cd "$VMACCT" && MVXACCOUNT=. "$TESTROOT/vmnatbin"); \
    printf 'COUNT ITM\n' | "$TCL" -a "$VMACCT" 2>&1; \
    "$TCL" -a "$VMACCT" -c 'MAP-MODE ITM mirror' 2>&1; \
    (cd "$VMACCT" && MVXACCOUNT=. "$TESTROOT/vmmirbin"); \
    "$TCL" -a "$VMACCT" -c 'MAP-MODE ITM native' 2>&1)"

  # native read (#34): in native mode READ recomposes the record from the
  # SQL columns/child rows, so an edit made straight to the tables (here via
  # psql, an "external" writer) is what the program reads.  Needs psql.
  if command -v psql >/dev/null 2>&1; then
    printf 'ORD @vpg\n' >> "$VMACCT/BINDINGS"
    "$TCL" -a "$VMACCT" -c 'DELETE-FILE ORD' >/dev/null 2>&1
    "$TCL" -a "$VMACCT" -c 'CREATE-FILE ORD USING @vpg' >/dev/null 2>&1
    cat > "$TESTROOT/vmord.b" <<'OEOF'
OPEN "ORD" TO F ELSE STOP
R = ""
R<1> = "Acme Corp"
R<2> = ICONV("25 JUL 2026", "D")
R<3> = "keep me"
R<5> = "Widget":@VM:"Gadget"
R<6> = "2":@VM:"1"
WRITE R ON F, "O1"
OPEN "DICT", "ORD" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Customer":@AM:"12L" ON D, "CUSTOMER"
WRITE "D":@AM:"2":@AM:"D4/":@AM:"When":@AM:"12R" ON D, "WHEN"
WRITE "D":@AM:"5":@AM:"":@AM:"Product":@AM:"10L":@AM:"ORDITEMS" ON D, "PRODUCT"
WRITE "D":@AM:"6":@AM:"MR0":@AM:"Qty":@AM:"5R":@AM:"ORDITEMS" ON D, "QTY"
OEOF
    "$MVX" "$TESTROOT/vmord.b" -o "$TESTROOT/vmordbin" 2>/dev/null
    (cd "$VMACCT" && MVXACCOUNT=. "$TESTROOT/vmordbin")
    "$TCL" -a "$VMACCT" -c 'CREATE-MAP ORD CUSTOMER WHEN PRODUCT QTY' \
      >/dev/null 2>&1
    "$TCL" -a "$VMACCT" -c 'MAP-MODE ORD native' >/dev/null 2>&1
    # external edits straight to the tables (id 'O1' = \x4f31)
    psql_ext "UPDATE vmtest.\"ORD\" SET \"CUSTOMER\"='Beta Ltd', \"WHEN\"=DATE '2027-01-15' WHERE id='\\x4f31'" >/dev/null
    psql_ext "UPDATE vmtest.\"ORD_ORDITEMS\" SET \"QTY\"=99 WHERE id='\\x4f31' AND seq=1" >/dev/null
    psql_ext "INSERT INTO vmtest.\"ORD_ORDITEMS\"(id,seq,\"PRODUCT\",\"QTY\") VALUES('\\x4f31',3,'Sprocket',5)" >/dev/null
    psql_ext "INSERT INTO vmtest.\"ORD\"(id,\"CUSTOMER\",\"WHEN\") VALUES('\\x4f39','SQL Only',DATE '2026-12-31')" >/dev/null
    cat > "$TESTROOT/vmordr.b" <<'REOF'
OPEN "ORD" TO F ELSE STOP
READ R FROM F, "O1" THEN
   PRINT "O1 ":R<1>:" | ":OCONV(R<2>,"D4/"):" | note=":R<3>
   PRINT "   items ":R<5>:" qty ":R<6>
END
READ R FROM F, "O9" THEN
   PRINT "O9 ":R<1>:" | ":OCONV(R<2>,"D4/")
END ELSE PRINT "O9 missing"
REOF
    "$MVX" "$TESTROOT/vmordr.b" -o "$TESTROOT/vmordrbin" 2>/dev/null
    check tcl-mapnread "$( \
      (cd "$VMACCT" && MVXACCOUNT=. "$TESTROOT/vmordrbin"); \
      printf 'COUNT ORD\n' | "$TCL" -a "$VMACCT" 2>&1)"

    # write diff (#35): an update projects only what changed.  Proven with
    # Postgres row xmin — a parent-only write leaves the child rows physically
    # untouched; a line-item change advances their xmin; an identical rewrite
    # touches nothing.  Fresh mirror-mode file, seeded via a program.
    printf 'OPT @vpg\n' >> "$VMACCT/BINDINGS"
    "$TCL" -a "$VMACCT" -c 'DELETE-FILE OPT' >/dev/null 2>&1
    "$TCL" -a "$VMACCT" -c 'CREATE-FILE OPT USING @vpg' >/dev/null 2>&1
    cat > "$TESTROOT/vmopt.b" <<'OPTEOF'
OPEN "OPT" TO F ELSE STOP
R = ""
R<1> = "Acme"
R<5> = "Widget":@VM:"Gadget"
R<6> = "2":@VM:"1"
WRITE R ON F, "P1"
OPEN "DICT", "OPT" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Customer":@AM:"12L" ON D, "CUSTOMER"
WRITE "D":@AM:"5":@AM:"":@AM:"Product":@AM:"10L":@AM:"OITEMS" ON D, "PRODUCT"
WRITE "D":@AM:"6":@AM:"MR0":@AM:"Qty":@AM:"5R":@AM:"OITEMS" ON D, "QTY"
OPTEOF
    "$MVX" "$TESTROOT/vmopt.b" -o "$TESTROOT/vmoptbin" 2>/dev/null
    (cd "$VMACCT" && MVXACCOUNT=. "$TESTROOT/vmoptbin")
    "$TCL" -a "$VMACCT" -c 'CREATE-MAP OPT CUSTOMER PRODUCT QTY' >/dev/null 2>&1
    xchild() { psql_ext "SELECT string_agg(xmin::text,',' ORDER BY seq) \
                 FROM vmtest.\"OPT_OITEMS\" WHERE id='\\x5031'"; }
    cat > "$TESTROOT/vmd1.b" <<'D1'
OPEN "OPT" TO F ELSE STOP
READ R FROM F, "P1" THEN R<1> = "Renamed Co"
WRITE R ON F, "P1"
D1
    cat > "$TESTROOT/vmd2.b" <<'D2'
OPEN "OPT" TO F ELSE STOP
READ R FROM F, "P1" THEN R<6,1> = "42"
WRITE R ON F, "P1"
D2
    cat > "$TESTROOT/vmd3.b" <<'D3'
OPEN "OPT" TO F ELSE STOP
READ R FROM F, "P1" THEN WRITE R ON F, "P1"
D3
    "$MVX" "$TESTROOT/vmd1.b" -o "$TESTROOT/vmd1bin" 2>/dev/null
    "$MVX" "$TESTROOT/vmd2.b" -o "$TESTROOT/vmd2bin" 2>/dev/null
    "$MVX" "$TESTROOT/vmd3.b" -o "$TESTROOT/vmd3bin" 2>/dev/null
    X0=$(xchild)
    (cd "$VMACCT" && MVXACCOUNT=. "$TESTROOT/vmd1bin")   # parent-only
    X1=$(xchild)
    (cd "$VMACCT" && MVXACCOUNT=. "$TESTROOT/vmd2bin")   # line-item
    X2=$(xchild)
    (cd "$VMACCT" && MVXACCOUNT=. "$TESTROOT/vmd3bin")   # identical
    X3=$(xchild)
    check tcl-mapdiff "$(printf '%s\n' \
      "parent-only leaves children: $([ "$X0" = "$X1" ] && echo yes || echo no)" \
      "line-item rewrites children: $([ "$X1" != "$X2" ] && echo yes || echo no)" \
      "identical write no-ops: $([ "$X2" = "$X3" ] && echo yes || echo no)" \
      "customer=$(psql_ext "SELECT \"CUSTOMER\" FROM vmtest.\"OPT\" WHERE id='\\x5031'")" \
      "qty1=$(psql_ext "SELECT \"QTY\" FROM vmtest.\"OPT_OITEMS\" WHERE id='\\x5031' AND seq=1")")"

    # native Postgres indexes on mapped columns (#37): CREATE-INDEX emits a
    # real SQL index and equality WITH pushes down to it — but only on an
    # identity-projected (TEXT, no-conv) column, else it falls back to the
    # scan so the result never differs.
    printf 'CIX @vpg\n' >> "$VMACCT/BINDINGS"
    "$TCL" -a "$VMACCT" -c 'DELETE-FILE CIX' >/dev/null 2>&1
    "$TCL" -a "$VMACCT" -c 'CREATE-FILE CIX USING @vpg' >/dev/null 2>&1
    cat > "$TESTROOT/vmcix.b" <<'CIXEOF'
OPEN "CIX" TO F ELSE STOP
WRITE "Widget":@AM:"NSW":@AM:"1500" ON F, "C1"
WRITE "Gadget":@AM:"VIC":@AM:"900" ON F, "C2"
WRITE "Sprocket":@AM:"NSW":@AM:"250" ON F, "C3"
OPEN "DICT", "CIX" TO D ELSE STOP
WRITE "D":@AM:"1":@AM:"":@AM:"Name":@AM:"12L" ON D, "NAME"
WRITE "D":@AM:"2":@AM:"":@AM:"State":@AM:"6L" ON D, "STATE"
WRITE "D":@AM:"3":@AM:"MD2":@AM:"Credit":@AM:"10R" ON D, "CREDIT"
CIXEOF
    "$MVX" "$TESTROOT/vmcix.b" -o "$TESTROOT/vmcixbin" 2>/dev/null
    (cd "$VMACCT" && MVXACCOUNT=. "$TESTROOT/vmcixbin")
    "$TCL" -a "$VMACCT" -c 'CREATE-MAP CIX NAME STATE CREDIT' >/dev/null 2>&1
    "$TCL" -a "$VMACCT" -c 'CREATE-INDEX CIX STATE' >/dev/null 2>&1
    "$TCL" -a "$VMACCT" -c 'CREATE-INDEX CIX CREDIT' >/dev/null 2>&1
    IXEXISTS=$(psql_ext "SELECT count(*) FROM pg_indexes WHERE schemaname='vmtest' AND indexname='CIX_STATE_idx'")
    # push-down proof: divert C2's STATE column to NSW in SQL only (rec still
    # VIC); if C2 appears, the SQL index ran, not a rec scan.
    psql_ext "UPDATE vmtest.\"CIX\" SET \"STATE\"='NSW' WHERE id='\\x4332'" >/dev/null
    check tcl-pgindex "$( \
      echo "STATE index exists: $IXEXISTS"; \
      echo "-- WITH STATE = NSW (index push-down, C2 diverted in SQL) --"; \
      "$TCL" -a "$VMACCT" -c 'LIST CIX NAME STATE WITH STATE = "NSW" BY NAME' 2>&1; \
      echo "-- WITH CREDIT = 1500 (converted column -> scan fallback) --"; \
      "$TCL" -a "$VMACCT" -c 'LIST CIX NAME CREDIT WITH CREDIT = "1500"' 2>&1; \
      "$TCL" -a "$VMACCT" -c 'DELETE-INDEX CIX STATE' 2>&1; \
      echo "STATE index after drop: $(psql_ext "SELECT count(*) FROM pg_indexes WHERE schemaname='vmtest' AND indexname='CIX_STATE_idx'")")"
  else
    echo "  (postgres psql-dependent map/index tests skipped — psql not found)"
  fi
else
  echo "  (postgres test skipped — set MVX_PG to run)"
fi

# ---------------------------------------------------------------- phase 3
# The sieve result is deterministic (Count 78498); only its *execution*
# on a loaded CI runner is occasionally flaky (a transient OOM during the
# LLVM compile leaves no binary).  Retry a few times and, on failure,
# surface the actual compile/run error instead of an empty "FAIL sieve:".
if [ "$QUICK" = 0 ]; then
  echo "== sieve"
  sieve_ok=0
  sieve_diag=""
  for attempt in 1 2 3; do
    if ! "$MVX" "$ROOT/bench/sieve.b" -o "$TESTROOT/sieve" \
         2>"$TESTROOT/sieve.err"; then
      sieve_diag="compile failed (attempt $attempt): $(cat "$TESTROOT/sieve.err")"
      continue
    fi
    sieve_out="$("$TESTROOT/sieve" 2>"$TESTROOT/sieve.err")"
    if printf '%s' "$sieve_out" | grep -q "Count: 78498" &&
       printf '%s' "$sieve_out" | grep -q "VALID"; then
      sieve_ok=1
      echo "  sieve valid ($(printf '%s' "$sieve_out" | head -1))"
      break
    fi
    sieve_diag="run output (attempt $attempt): ${sieve_out:-<empty>} $(cat "$TESTROOT/sieve.err")"
  done
  if [ "$sieve_ok" = 1 ]; then
    PASS=$((PASS + 1))
  elif [ -n "${MVX_SIEVE_OPTIONAL:-}" ]; then
    # CI sets this: the sieve is a perf benchmark, not a correctness gate,
    # and is sensitive to runner contention — report but do not fail.
    echo "WARN sieve (non-fatal): $sieve_diag"
  else
    echo "FAIL sieve: $sieve_diag"
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
