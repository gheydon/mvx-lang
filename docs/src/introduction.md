# Introduction

MVX is a native compiler and runtime environment for Pick/MultiValue
BASIC. It compiles BASIC straight to machine code through LLVM — no
transpilation to C — and surrounds the compiler with the pieces that
make a MultiValue system a system: a pluggable storage layer, a
classic TCL shell, dictionaries and queries, secondary indexes, a
package ecosystem, and full-screen terminal support.

The reference dialect is **traditional Pick BASIC** (classic Pick /
R83 style). Wherever MultiValue platforms diverge, classic Pick
behaviour is the tie-breaker. Extensions are admitted only where
classic Pick has no equivalent, and each is documented as such — the
two most visible being C-style comments and the modern terminal
primitives.

Highlights:

- **A real compiler.** Programs and subroutines compile to native
  executables and shared libraries with DWARF debug information mapped
  to BASIC source lines — `lldb` sets breakpoints like `b prog.b:12`.
  Numeric code compiles to native integer and floating-point
  instructions; the standard prime-sieve benchmark runs at ~99% of an
  equivalent C program.
- **Pluggable storage.** MV files bind to backends through a small
  driver contract. Two drivers ship: embedded LMDB and plain
  directories (attributes as lines — the git-friendly shape). A third,
  `lmdbnet`, reaches an `mvxd` daemon over the network; switching an
  account from embedded to networked is one environment variable.
- **The classic environment.** TCL with VOC dispatch, `LIST`/`SELECT`
  queries driven by dictionaries, select lists that flow between
  commands, `ED`, and the full verb set — with all verbs written in
  MVX BASIC itself.
- **An ecosystem.** Packages with dependency manifests, a
  Cobra-style command framework, runtime-resolved subroutine
  libraries, docblock metadata queryable through the dictionary, and a
  VSCode extension.

## Where things live

| | |
|---|---|
| `mvx-basic` | compiler and link driver |
| `mvx` | the classic TCL shell |
| `mvxd` | the networked storage daemon |
| `verbs/` | standard verbs (BASIC source) |
| `packages/` | shipped packages: `cmd`, `git`, `sample` |
| `examples/` | FSDEMO, SNAKE |
| `ARCHITECTURE.md` | the full design document |
| `DECISIONS.md` | settled implementation decisions |
