# MVX — Settled decisions

## Slice 2 — storage

- **The driver contract lives in `mvx_driver.h`** and is exactly the
  minimal set from ARCHITECTURE.md 4.1: open/close, read/write/delete,
  select (snapshot cursor). Locks are NOT in the contract — they live in
  the runtime lock table (`mvx_store.c`), keyed by file spec + record
  id, because READU can span user think-time and must never pin a
  backend transaction.
- **Drivers are dlopen'd shared libraries** (`libmvxdrv_<name>.dylib`),
  loaded on first use via the single exported entry point
  `mvx_driver_entry(int abi)` with an ABI-version handshake
  (`MVX_DRIVER_ABI`). Backend dependencies link into the driver library
  — liblmdb is a dependency of `libmvxdrv_lmdb`, not of compiled
  programs, so it loads exactly when the driver does. Search path:
  `$MVXDRIVERS` (colon-separated), then the built-in driver directory
  baked in at build time. A missing or ABI-incompatible driver is a
  loud fatal error, not an OPEN ELSE — configuration breakage must not
  masquerade as a missing file.
- **File resolution**: account root is `$MVXACCOUNT` (default cwd). A
  spec naming an existing directory opens the directory driver
  (attributes ↔ lines, one record per file — the git-native shape);
  anything else is a named DB in the account's LMDB environment at
  `<account>/mvxdata.lmdb`. One env per account, one named DB per file,
  short transactions, copy-out reads, 511-byte key validation.
- **File variables** are a fifth value tag (`MV_FILE`), holding the
  driver handle pointer; handles are context-owned and closed at exit.
  WRITE releases the record lock, WRITEU keeps it — both after a
  successful driver write.
- **Dictionaries are sibling stores, resolved by naming convention.**
  `OPEN "DICT","X"` opens `DICT.X` (LMDB named DB) or `X/.DICT`
  (hidden subdirectory of a directory file — invisible to data SELECTs,
  which skip dotfiles). Every statement then works on a dict handle
  unchanged, because a dictionary is just another record store.
  `CREATEFILE` creates DICT and DATA together, classic style;
  `DELETEFILE` removes both. Dictionary *semantics* (D-items driving
  LIST/SELECT and indexing) build on this in later slices.
- **File creation is explicit.** `OPEN` never creates; a nonexistent
  file takes ELSE, classic style. The driver contract carries handle-
  less `create`/`remove` operations, surfaced in BASIC as
  `CREATEFILE(spec {,"DIR"})` and `DELETEFILE(spec)` — these are the
  primitives the CREATE-FILE / DELETE-FILE verbs will wrap when TCL
  arrives, since verbs are BASIC programs, not C.

## Slice 3 — TCL

- **The C shell is dispatch only.** `mvx-tcl` implements the prompt,
  the builtin table (OFF/QUIT/BYE, `!`), VOC lookup, and fork/exec of
  cataloged executables — nothing else. Verbs are compiled BASIC
  programs in `CATALOG/`, named by VOC records (attr 1 `V`, attr 2
  executable path). Dispatch order: builtins, VOC, not-found.
- **The sentence crosses via the environment**: TCL sets
  `$MVX_SENTENCE`; the `SENTENCE()` intrinsic reads it. Verbs parse
  their own arguments with FIELD().
- **The privilege gate lives in `mvx_exec.c`, in the runtime.** One
  gate covers every spawn path: TCL's `!`/SH builtins, EXECUTE, and the
  compiler. Tiers per 8.2 (restricted < developer < unrestricted,
  default deny) come from `$MVXPRIV` — the development stand-in for
  system config outside the account; the property that matters is that
  account data cannot write it. Spawning cataloged verbs is allowed at
  every tier; raw Unix needs unrestricted; compiling needs developer.
  All spawns are argv-style (`execv`), never through a shell, except
  the explicitly-unrestricted raw passthrough.
- **EXECUTE spawns `mvx-tcl -c`** so there is exactly one dispatcher in
  the system. CAPTURING collects stdout as a dynamic array (line ↔
  attribute); RETURNING receives the exit status (deviation from
  classic error-number lists, documented). Select-list passing across
  EXECUTE is deferred until session-state classification (6.6) exists.
- **Select lists cross processes through the session file.** `mvx-tcl`
  owns `$MVXSESSION` (created only when not inherited, so nested
  EXECUTE shares the outer session). A program exiting with an
  unconsumed select list persists the remainder there; the next
  program's first READNEXT consumes it, exactly once. `SYSTEM(11)`
  reports whether a list is active; query verbs use the active list
  instead of re-selecting, classic style. This is the session/
  select-list seam of ARCHITECTURE.md 7.3 — replacing the file with a
  session service is a config change, not surgery.
- **LIST and SELECT are BASIC verbs** driven by dictionary D-items
  (1=D, 2=attr#, 3=OCONV conversion, 4=heading, 5=format "12L"/"8R").
  WITH filters, BY sorts via ordered LOCATE insertion — using AR
  (numeric) ordering when the BY item's dict format is R-justified.
  SELECT installs its filtered ids with FORMLIST and exits, leaving
  the list for the next command.
- **`COMPILE(mode, src, out)`** is the narrow developer-tier primitive
  behind the BASIC and CATALOG verbs: structured arguments, argv built
  by the runtime, nothing to inject. BASIC compiles `FN ITEM` to
  `FN.O/ITEM.o`; CATALOG links to `CATALOG/ITEM` and writes the VOC
  entry — compile and publish stay separate verbs, classic style.
- **Account = parameter, not mode**: `-a` flag, then `$MVXACCOUNT`,
  then cwd; the shell chdirs to the account and children resolve
  relative to it. `-c` runs one sentence for ssh/cron use.

# Slice 1 decisions

Concrete resolutions of the two open decisions in `ARCHITECTURE.md` §3.3,
plus the smaller choices they force. These are load-bearing: the ABI ones
are permanent once separately compiled subroutines exist.

---

## Decision A — value representation

**Chosen: boxed value with numeric tags (option 1), plus compiler numeric
specialisation (option 3) — both implemented.**

The specialisation layer (in `compiler/src/codegen.cpp`, `NumericAnalysis`):
a scalar or DIM'd array is specialised when every value stored into it is
a provably numeric expression and it never escapes by reference (CALL
argument / subroutine parameter). The analysis is a fixed point over the
lattice Int < Dbl < NotNum:

- **Int tier** — provably integral: bare `i64` alloca, native integer
  ops. Division always yields Dbl (MV `/` is fractional); `INT()` is the
  idiom that brings a quotient back to the Int tier. Known deviation:
  `i64` arithmetic wraps on overflow where boxed arithmetic promotes to
  double — accepted for Slice 1.
- **Dbl tier** — provably numeric: bare `double` alloca, native FP ops.
  Boxed arithmetic already promotes through double and compares
  numerically via double, so this tier is exact to 2^53.
- **Arrays** get a storage class: `i8` buffer when every store is an
  integer literal in 0..255 (flag arrays), `i64` for integral stores,
  `f64` for numeric stores, boxed otherwise.

Everything else falls back to the boxed representation below; boxing at
the seam uses `mv_set_int` for Int-kind values so printed output is
indistinguishable from the boxed path.

Sieve result (1M sieve, 5 s, Apple M-series): boxed-only 404 passes;
double tier 5,767; int/byte tier **14,509 vs 14,652 for the equivalent C
byte-array sieve — 99% of C** — with the frontend untouched throughout.
The value-representation bet ARCHITECTURE.md 3.3 makes is confirmed.

```c
typedef struct mv_string {          /* immutable, refcounted */
    int64_t refs;
    int64_t len;
    char    data[];                 /* NUL-terminated for convenience */
} mv_string;

typedef struct mv_value {
    int64_t    tag;                 /* MV_UNASSIGNED / MV_INT / MV_DBL / MV_STR */
    int64_t    i;                   /* valid when tag == MV_INT */
    double     d;                   /* valid when tag == MV_DBL */
    mv_string *s;                   /* owned ref when tag == MV_STR, else NULL */
} mv_value;                         /* 32 bytes, fixed layout — part of the ABI */
```

Key properties:

- **Numbers stay numeric.** `I = 5` sets `MV_INT`; arithmetic on two
  numeric tags never touches a string. Stringification happens lazily
  (PRINT, concat). This alone avoids the 100x string-round-trip cliff
  while remaining fully boxed and correct.
- **Strings are immutable and refcounted**, so copy/assign is a retain,
  not a heap copy.
- **Field layout is frozen and known to the compiler.** Codegen may load
  `tag`/`i`/`d` directly (fast paths) but all mutation goes through
  runtime calls. This is the seam where option 3 (type specialisation)
  plugs in later: the IR emitter works through a `ValueRef` abstraction so
  a provably-numeric variable can become a bare `i64` alloca without
  touching the parser or AST.
- Numeric string comparison follows MV rules: if both operands look
  numeric, compare numerically; otherwise byte-wise string compare.
- `MV_UNASSIGNED` coerces to 0 / "" with a runtime warning to stderr
  ("zero used", classic Pick style), not a hard error.

## Decision B — subroutine ABI (permanent)

```c
void mvx_sub_<NAME>(mvx_ctx *ctx, int32_t argc, mv_value **argv);
```

- **Hidden context parameter first, always** — present from day one even
  though Slice 1 only uses it for output state. Session state, locks, and
  the privilege gate ride on it later without an ABI break.
- **Every argument is `mv_value*`** pointing at the caller's slot —
  `CALL SUB(A, B)` is by-reference, callee mutation is visible to the
  caller. A non-lvalue argument (expression, literal) is materialised
  into a caller temp and passed by pointer; mutation of it is legal and
  discarded, matching MV behaviour.
- **Arity is checked at runtime, at call entry** (`argc` vs declared
  count); mismatch is a fatal runtime error naming the subroutine.
  Traditional MV defers arity failure to runtime; we keep that but fail
  fast and loud. Link-time checking can be layered on later without an
  ABI change since `argc` stays in the signature.
- **Name mangling: `mvx_sub_` + subroutine name as written** (MV names
  are conventionally uppercase; the name is taken verbatim from the
  `SUBROUTINE` statement). Flat C namespace, `dlsym`-friendly.
- Main programs compile to `void mvx_main(mvx_ctx *ctx)`; a tiny runtime
  crt provides the real `main()`, creates the context, calls `mvx_main`.

## Smaller settled choices

- **Reference dialect: traditional Pick BASIC** (classic Pick / R83
  style). Wherever MV platforms diverge, classic Pick behaviour is the
  tie-breaker: `IF ... THEN ... END ELSE ... END` block form, `=`/`#`
  comparators, `LOOP`/`UNTIL`/`WHILE`/`DO`/`REPEAT`, 1-based `DIM`,
  warn-and-zero on unassigned variables, PRECISION 4 output. Later MV
  extensions are admitted only where classic Pick has no equivalent
  (e.g. `SYSTEM(12)` millisecond clock for benchmarking, since classic
  `TIME()` is whole seconds). Numeric statement labels, `GOTO`/`GO TO`,
  and `GOSUB`/`RETURN` are implemented: labels compile to basic blocks,
  GOSUB keeps a 1024-deep return stack dispatched on RETURN, and RETURN
  with an empty stack ends the program (or returns to the caller in a
  subroutine). `STOP` terminates the whole program even from inside a
  subroutine. FOR-loop state lives in stack slots rather than SSA values
  so jumps into loop bodies stay well-formed; mem2reg promotes them back
  in label-free code, so the sieve pass rate is unchanged.
- **Dynamic arrays** live in the boxed string representation (marks
  0xFE/0xFD/0xFC). `A<a,v,s>` parses by attempting the extraction and
  backtracking to less-than when it does not close with `>`; subscripts
  parse at additive precedence, so comparisons inside subscripts need
  parentheses — the same resolution classic MV compilers use.
  Assigning through `A<...>` demotes the base from the numeric tiers,
  since the value then carries marks. `BEGIN CASE` desugars to a nested
  IF chain in the parser; there is no CASE node in codegen.
- **COMMON is context-owned, positional, always boxed.** Blocks (unnamed
  and `/NAME/`) live in `mvx_ctx`, so all programs in a process share
  them through the hidden context parameter — no process-global state.
  Slot storage is chunked and never realloc'd: compiled code binds slot
  addresses once at function entry, so addresses must stay stable as
  later programs extend a block. COMMON variables never specialise.
- **Runtime is C11** (clean frozen ABI, no C++ mangling in the contract);
  the compiler is C++17 against the LLVM C++ API.
- **Arrays**: `DIM A(n[,m])`, 1-based, bounds-checked, elements are
  `mv_value` slots. A distinct `mv_array` heap object, not a dynamic
  string.
- **Timing intrinsics**: `TIME()` → integer seconds since midnight;
  `SYSTEM(12)` → milliseconds since midnight (jBASE/UniVerse-compatible),
  which is what the sieve's 5-second loop uses.
- **DWARF**: emitted always (no `-g` flag needed to opt in),
  `DW_LANG_BASIC`, one `DISubprogram` per program/subroutine, line table
  against the `.b` source.
- **Driver**: `mvx -c prog.b -o prog.o` (object), `mvx prog.b -o prog`
  (compile+link executable), `mvx -shared sub.b -o libsub.dylib`.
  Errors to stderr as `item:line: message`.
