# Getting Started

## Build

Requires CMake, Ninja, LLVM, and LMDB (`brew install llvm cmake ninja
lmdb`; libedit ships with macOS).

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/llvm
ninja -C build
```

This produces `build/bin/{mvx,mvx-tcl,mvxd}`, the runtime and storage
drivers in `build/lib/`, and the **system account** — the compiled
standard verbs and master VOC — in `build/system/`.

Run the test suite any time with:

```sh
ninja -C build check
```

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
build/bin/mvx hello.b -o hello && ./hello
```

Compile errors arrive on stderr as `item:line: message`. Debugging
works out of the box: `lldb ./hello`, then `b hello.b:5`, `run`.

## Your first account

An account is a directory holding MV files. Create one and log on:

```sh
scripts/mkaccount.sh myaccount
build/bin/mvx-tcl -a myaccount
```

The prompt shows the account name. Standard verbs come from the
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
MVXPRIV=developer build/bin/mvx-tcl -a myaccount
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
