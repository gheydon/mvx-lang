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
- **Two-part `OPEN "DICT","F"`** parses but fails (ELSE) until
  dictionaries exist.
- **File creation is explicit.** `OPEN` never creates; a nonexistent
  file takes ELSE, classic style. The driver contract carries handle-
  less `create`/`remove` operations, surfaced in BASIC as
  `CREATEFILE(spec {,"DIR"})` and `DELETEFILE(spec)` — these are the
  primitives the CREATE-FILE / DELETE-FILE verbs will wrap when TCL
  arrives, since verbs are BASIC programs, not C.

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
