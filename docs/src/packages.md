# Packages

A package is an account-shaped directory that accounts **link** to
gain its verbs and subroutines:

```
mypkg/
  PKG           manifest: name / version / description / dependencies
  BP/           source (mains become verbs, SUBROUTINEs become library)
  VOC/          one text record per verb:  V  then  CATALOG/<name>
  CATALOG/      built verb executables      (mkpkg output)
  LIB/          built subroutine library    (mkpkg output)
```

Build with `scripts/mkpkg.sh mypkg` — main programs compile into
`CATALOG/`, and all `SUBROUTINE` sources bundle into one shared
library `LIB/lib<name>` (one `dlopen`, one artifact).

## Linking

```
> LINK-PKG /path/to/git
linked /path/to/git
linked /path/to/cmd          <- dependency, auto-resolved
> LIST-PKGS
git@1.0   ok   /path/to/git   requires cmd
cmd@1.0   ok   /path/to/cmd
```

The `PKG` manifest is a plain record: attribute 1 name, 2 version, 3
description, 4 onward dependency names. `LINK-PKG` links the whole
dependency closure — satisfying each dependency against already
linked packages, then a directory of that name beside the requiring
package, then `$MVXPKGPATH` — and refuses cleanly when one cannot be
found. `UNLINK-PKG` refuses while another linked package depends on
the target.

Linked packages join verb resolution (after the local VOC, before the
system account) and their `LIB/` joins `CALL` resolution. Package
verbs run **in your account**, on your data. A `LINK-PKG` takes
effect in the same session.

## The cmd framework

`packages/cmd` is a Cobra-style command framework:

```
CALL CMD.INIT("GIT", "work with the account's git repository")
CALL CMD.ADD("STATUS", "working tree status", "GIT.STATUS")
CALL CMD.ADD("LOG", "recent history", "GIT.LOG")
CALL CMD.RUN
```

`CMD.RUN` parses the sentence, dispatches to the handler subroutine
(via the indirect `CALL @VAR`), and generates help for `help`,
no-argument, and unknown-command cases. `packages/git` is the
reference consumer — a `GIT` verb with subcommands in ~10 lines plus
one small handler subroutine each.

## Native subroutines

A package can ship subroutines written in C, not just BASIC: a C
function matching the subroutine ABI
(`void mvx_sub_NAME(mvx_ctx *, int32_t argc, mv_value **argv)`) is
CALLed exactly like a BASIC subroutine. Put the sources in the
package and build them into `LIB/` with a `build-native.sh` script
(mkpkg runs it); link any native dependency into that library alone.
The git package does this — its git operations are libgit2-backed
native subroutines, so libgit2 burdens neither the runtime nor
programs that never use git.

## Subroutine libraries

`CATALOG BP MYSUB` for a `SUBROUTINE` source builds `LIB/MYSUB` in
the account instead of a verb. `CALL` resolves at runtime: symbols
already in the program, then the account's `LIB/`, each linked
package's `LIB/`, then the system `LIB/`. The subroutine ABI is
frozen, so libraries built at different times interoperate.

## bin commands

A package may ship standalone OS commands in a `bin/` directory —
executables that run from the shell, not from TCL. `mkpkg.sh` links
whatever it finds there onto the dev `PATH` (`build/bin`, beside `mvx`);
a real install would copy them into a system bin directory instead.
The git package uses this to ship `mvx-git` (built from C by its
`build-native.sh`). See [Version Control](git.md) for `mvx-git`.
