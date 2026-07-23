# MVX

A native compiler and runtime for Pick/MultiValue BASIC. BASIC source
compiles straight to object code via LLVM — no C transpilation — with
DWARF debug info against BASIC source lines.

See `ARCHITECTURE.md` for the full design and `DECISIONS.md` for settled
implementation decisions.

## Status — Slice 1 milestone reached

Compiler + minimal runtime. The prime-sieve milestone (1M sieve,
5-second timed run, per [Primes](https://github.com/PlummersSoftwareLLC/Primes)
rules) validates at 78,498 primes and runs at ~14,500 passes/5s on Apple
M-series — 99% of an equivalent C sieve — via two tiers of compiler
numeric specialisation (native `i64`/`double` scalars, `i8`/`i64`/`f64`
array storage) over the boxed value representation. See `DECISIONS.md`.

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

## Layout

- `compiler/` — lexer, parser, LLVM codegen, `mvx` driver (C++17)
- `runtime/` — boxed value type, arrays, printing, intrinsics (C11);
  `mvx_runtime.h` is the permanent ABI surface
- `bench/sieve.b` — the Slice 1 milestone benchmark
- `tests/` — subset smoke tests
