# Getting Started

## Build

Requires CMake, Ninja, LLVM, and LMDB (`brew install llvm cmake ninja
lmdb`; libedit ships with macOS).

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/llvm
ninja -C build
```

This produces `build/bin/{mvx-basic,mvx,mvx-lmdbd}`, the runtime and storage
drivers in `build/lib/`, and the **system account** — the compiled
standard verbs and master VOC — in `build/system/`.

Run the test suite any time with:

```sh
ninja -C build check
```

## Install

`cmake --install` lays the build tree down as a self-contained,
relocatable prefix:

```sh
cmake --install build --prefix ~/.local
```

It writes:

| path | contents |
|---|---|
| `<prefix>/bin` | `mvx`, `mvx-basic`, `mvx-lmdbd`, `mvx-convert-acct` |
| `<prefix>/lib` | `libmvxrt`, the storage drivers, `mvx_crt.o` |
| `<prefix>/share/mvx/system` | the standard verbs and master VOC |

The standard verbs are re-cataloged with the installed compiler as part
of the install, so their runtime paths point at `<prefix>/lib`. Put
`<prefix>/bin` on your `PATH` and `mvx` runs from anywhere — it finds the
runtime, drivers, and system account **relative to its own location**, so
no environment variables are needed and the whole prefix can be moved or
renamed as a unit. The `MVXSYSTEM`, `MVXDRIVERS`, and `MVXBIN` variables
still override those locations when you want them to.

## Your first program

```sh
cat > hello.b <<'EOF'
/**
 * @file hello
 */
PRINT "hello from MVX"
FOR I = 1 TO 3
   PRINT "counting ":I
NEXT I
EOF
build/bin/mvx-basic hello.b -o hello && ./hello
```

Compile errors arrive on stderr as `item:line: message`. Debugging
works out of the box: `lldb ./hello`, then `b hello.b:5`, `run`.

## Your first account

An account is a directory holding MV files. Log on to a directory:

```sh
build/bin/mvx -a myaccount
```

If the directory is not yet an MVX account, an interactive session
asks before creating one (UniData-style):

```
Directory /path/myaccount is not an MVX account.
Create one here? (y/N) y
Created MVX account in /path/myaccount
```

Answer `n` and nothing is created. `scripts/mkaccount.sh myaccount`
still creates an account non-interactively. Either way a `.mvx`
descriptor file marks the directory as an account — the authoritative
marker, since the VOC itself may be a named database inside the LMDB
environment or on a daemon rather than a file on disk. The prompt shows the
account name. Standard verbs come from the
system account; try:

```
myaccount> CREATE-FILE CUSTOMERS
myaccount> ED CUSTOMERS C1
  (I to insert lines, "." ends input, FI files the record)
myaccount> CT CUSTOMERS C1
myaccount> LISTF
myaccount> OFF
```

## Writing and cataloging programs

Source lives in a directory file, conventionally `BP`:

```
myaccount> CREATE-FILE BP DIR
```

Edit `myaccount/BP/MYPROG` with any editor (it is a plain text file),
then — with developer privilege — compile and publish it as a verb:

```sh
MVXPRIV=developer build/bin/mvx -a myaccount
myaccount> BASIC BP MYPROG        (compile only - syntax check)
myaccount> CATALOG BP MYPROG      (compile, link, add to the VOC)
myaccount> MYPROG SOME ARGS
```

A source whose first statement is `SUBROUTINE` catalogs into `LIB/` as
a shared library instead, resolved at `CALL` time.

## Privilege tiers

`$MVXPRIV` selects the tier: unset is **restricted** (no compiling, no
Unix escapes), `developer` adds `BASIC`/`CATALOG`/`COMPILE()`, and
`unrestricted` adds `!` and `SH`. The default is deny.
