# Tooling

## The mvx-basic compiler driver

```sh
mvx-basic prog.b -o prog                 # compile and link an executable
mvx-basic -c prog.b -o prog.o            # compile only
mvx-basic -shared sub.b -o libsub        # subroutine shared library
mvx-basic main.b sub1.b sub2.b -o prog   # multi-source build
mvx-basic -O0|-O1|-O2 ...                # optimisation (default -O2)
mvx-basic --emit-llvm ...                # also write textual IR
mvx-basic -D NAME[=value] ...      define a preprocessor symbol
```

Errors: `item:line: message` on stderr. DWARF debug info is always
emitted — `lldb prog`, `b prog.b:12`, `run` steps BASIC source, even
optimised.

## Testing

```sh
ninja -C build check               # or scripts/test.sh  (-q skips the sieve)
scripts/test.sh --bless            # re-capture expected outputs
```

Language tests diff compiled programs against `tests/expected/`;
system tests run scripted TCL sessions in a throwaway account
(queries, ED, packages, the gate, indexes, the daemon with its lock
lease); the sieve benchmark asserts correctness.

## VSCode

`editors/vscode/mvx-basic` — symlink it into `~/.vscode/extensions`:
syntax highlighting (docblock `@tags` included), comment toggling,
auto-indent, snippets, and the `$mvx` problem matcher. The repo's
`.vscode/tasks.json` binds Cmd+Shift+B to compile the current file
with clickable errors. Files ending `.b`, or inside `BP/`, `*.BP/`,
or `*_BP/` directories, open as MVX BASIC.

## This book

Markdown sources in `docs/src`, built with mdBook:

```sh
mdbook build docs                  # HTML into build/docs
mdbook serve docs                  # live-reload while writing
```

The same Markdown can feed pandoc for a typeset PDF when a print
edition is wanted.
