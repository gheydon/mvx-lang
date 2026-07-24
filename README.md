# MVX

A native compiler and runtime environment for Pick/MultiValue BASIC.
BASIC source compiles straight to object code via LLVM — no C
transpilation — with DWARF debug info against BASIC source lines, and
the compiler is surrounded by the pieces that make a MultiValue system
a system: pluggable storage (embedded LMDB, a networked daemon, plain
directories), a classic TCL shell, dictionaries and dictionary-driven
`LIST`/`SELECT` queries, secondary indexes, a package ecosystem, and
first-class git version control of hash-file records.

See `ARCHITECTURE.md` for the full design, `DECISIONS.md` for settled
implementation decisions, and `docs/` for the book (`mdbook serve
docs`).

## Seeking Pick/MultiValue products to port

**I am looking for real Pick-based products to port to MVX** — the goal
is to make MVX more feature-complete and robust by exercising it
against actual application code rather than synthetic tests. Real
programs surface the dialect corners, intrinsic coverage, and
operational edges that matter.

If you have a Pick/MultiValue application (UniVerse, UniData, jBASE,
D3, Reality, or similar) that you would be willing to let me test
against, please get in touch. **I am happy to sign an NDA** to get
access to code for testing.

To reach me, either:

- **Open a GitHub issue** at
  [github.com/gheydon/mvx-lang/issues](https://github.com/gheydon/mvx-lang/issues),
  or
- **Email Gordon Heydon — gordon@heydon.com.au**.

Bug reports, feature requests, and dialect-compatibility notes are also
welcome the same way.

## Status

The compiler and the environment are both well advanced. Highlights:

- **Native compiler** — programs and subroutines to executables and
  shared libraries, DWARF debug info (`lldb` steps BASIC source). The
  1M prime-sieve benchmark runs at ~99% of an equivalent C program via
  two tiers of numeric specialisation over the boxed value
  representation.
- **Storage** — a pluggable driver contract with embedded LMDB, a
  networked `mvxd` daemon, and directory backends; dictionaries,
  secondary indexes, record locks, and select lists; mixed
  local/remote per file (`CREATE-FILE … USING <driver>`).
- **Environment** — classic TCL with VOC dispatch, the standard verb
  set (all written in MVX BASIC), `EXECUTE` and a runtime privilege
  gate, packages with dependency manifests, full-screen terminal
  support, and git version control of records (branch/merge/cherry-pick
  for multi-site delivery).

## Build

Requires CMake, Ninja, and LLVM (`brew install llvm cmake ninja`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/llvm
ninja -C build
```

Produces `build/bin/mvx`, `build/bin/mvx-tcl`, the runtime in
`build/lib/`, and the system account (standard verbs) in
`build/system/`.

## Tests

```sh
ninja -C build check          # or: scripts/test.sh   (-q skips the sieve)
```

Language tests diff `tests/*.b` output against `tests/expected/`;
system tests run scripted TCL sessions in a throwaway account. After an
intentional behaviour change, re-capture with `scripts/test.sh --bless`
and review the diff.

## Use

```sh
build/bin/mvx prog.b -o prog          # compile + link executable
build/bin/mvx -c prog.b -o prog.o     # compile to object only
build/bin/mvx -shared subs.b -o libsubs.dylib   # subroutine library
build/bin/mvx prog.b subs.b -o prog   # main + subroutines together
```

Compile errors go to stderr as `item:line: message`.

Source-level debugging works out of the box:

```sh
lldb prog
(lldb) b prog.b:12
(lldb) run
```

## Trying it

```sh
build/bin/mvx tests/smoke.b -o /tmp/smoke && /tmp/smoke
build/bin/mvx bench/sieve.b -o /tmp/sieve && /tmp/sieve
```

## The environment

Create an account and log on to the classic shell:

```sh
scripts/mkaccount.sh myaccount
build/bin/mvx-tcl -a myaccount
```

```
myaccount> CREATE-FILE CUSTOMERS
myaccount> LIST CUSTOMERS
myaccount> OFF
```

Requires LMDB (`brew install lmdb`) for storage and, for the git
package, libgit2 (`brew install libgit2`). See `docs/` for the full
book.

## Layout

- `compiler/` — lexer, parser, LLVM codegen, `mvx` driver (C++17)
- `runtime/` — value type, arrays, storage, intrinsics (C11);
  `mvx_runtime.h` is the permanent ABI surface
- `daemon/` — `mvxd`, the networked LMDB storage daemon
- `tcl/` — `mvx-tcl`, the classic shell
- `verbs/` — the standard verb set (MVX BASIC)
- `packages/` — shipped packages (`cmd`, `git`, `sample`)
- `docs/` — the MVX book (mdBook)
- `bench/sieve.b` — the prime-sieve benchmark
- `tests/`, `scripts/test.sh` — the test harness
