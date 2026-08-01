# json — portable (UniData/BASIC) port

The json package is **native on mvx** (`src/mvxjson.c` → `libmvxext_json`;
`JSONENCODE`/`JSONDECODE` exports) and `MAPFIELD` is a compiler builtin. This
directory is the **portable BASIC** implementation for hosts without those
intrinsics (UniData first) — so mvx-lang/json is one cross-platform package:
native where it can be, BASIC where it must.

- **`MAPFIELD(name, attr, conv, type, assoc)`** — builds one `%MAP%` spec field
  `name<VM>attr<VM>conv<VM>type<VM>assoc` (type derived from conv when empty),
  byte-for-byte what the mvx builtin emits. Pure string work; the reusable
  spec builder for **any** spec-driven decoder — JSON here, a YAML decoder or a
  dictionary projection later.
- **`JSONDECODE(json, spec)`** — decodes per the spec: flat `"key":"value"`
  scalars, and one **association** (an array of flat objects under the assoc
  key) into parallel multivalued attributes (`R<pos,i>`). Enough for the common
  shapes (package metadata, the registry's `/search`); the native mvx decoder
  does the general case.

`type` from conv: `MD/MR/ML → NUMERIC`, `MT → TIME`, `D… → DATE`, else `TEXT`.

## Note — MVPKG bundles its own copy

The MultiValue package manager (mvx-lang/mv_package) can't *depend* on this
package to reach the registry, so it ships an equivalent seam of its own
(`udt/MAPFIELD`, `udt/JSONDECODE`) for bootstrap — the same relationship as its
bundled `CMD.BP` vs the `cmd` package. This port is the canonical source; keep
the two in step.

## Not yet ported

`JSONENCODE` (MV → JSON) — the mvx side has it; the BASIC encode side is a TODO.
Values are read as the text between the first quote pair after a key (no escape
handling), which is sufficient for the registry's responses.
