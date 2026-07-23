#!/bin/sh
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
MVX="$ROOT/build/bin/mvx"
TCL="$ROOT/build/bin/mvx-tcl"
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
for t in store storedir dict; do
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

# self-hosting: BASIC + CATALOG a verb, then run it
mkdir -p "$ACCT/BP" "$ACCT/BP/.DICT"
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
printf 'I\nDOCTAG(file)\n\nFile\n10L\n' > "$ACCT/BP/.DICT/FILE"
printf 'I\nDOCTAG(version)\n\nVersion\n8L\n' > "$ACCT/BP/.DICT/VERSION"
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

# the networked daemon: same account flow through mvxd, plus the lock
# lease (holder dies without releasing; next session proceeds)
DSOCK="/tmp/mvxd-test-$$.sock"
DACCT="$TESTROOT/dacct"
mkdir -p "$DACCT"
"$ROOT/build/bin/mvxd" -d "$TESTROOT/ddata" -s "$DSOCK" 2>/dev/null &
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

echo "== $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
