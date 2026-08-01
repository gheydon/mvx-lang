# mapfield

`MAPFIELD` — the portable **`%MAP%` spec builder**. A *mapping* says how a
document's fields become MultiValue attributes; `MAPFIELD` builds one field of
that spec, and spec-driven decoders (`json` now, `yaml`/`xml` later) consume it.
Factored into its own package so every format decoder **depends on** it rather
than carrying a copy.

```
SPEC<-1> = MAPFIELD(name, attr, conv, type, assoc)
```

builds `name<VM>attr<VM>conv<VM>type<VM>assoc`:

- **attr** — the MV attribute the field lands at.
- **type** — `TEXT`/`NUMERIC`/`DATE`/`TIME`; derived from **conv** when empty
  (`MD/MR/ML → NUMERIC`, `MT → TIME`, `D… → DATE`, else `TEXT`).
- **assoc** — empty = a single-valued (scalar) field; else the association name,
  whose members decode as **parallel multivalues** (an array of objects).

## Native on mvx, BASIC on udt

On **mvx**, `MAPFIELD` is a **compiler builtin** (`mvx_map_field`) — fast, always
present. On **udt** (and any host without the builtin), it's the pure-BASIC
`udt/MAPFIELD` here. A **portable** program that compiles on both selects with a
compile directive:

```basic
$IFDEF MVX
   * MAPFIELD is the compiler builtin — nothing to declare
$ELSE
   DEFFUN MAPFIELD(A, B, C, D, E)   ; * the mapfield package's cataloged function
$ENDIF
```

(Platform-specific source — an mvx-only or udt-only file — needs no directive.)

## Consumers

`json` depends on `mapfield` and pairs it with a decoder (`JSONDECODE`).
A `yaml` or `xml` package would do the same — one shared projection layer, one
parser each.
