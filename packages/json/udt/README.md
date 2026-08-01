# json — portable (UniData/BASIC) port

The json package is **native on mvx** (`src/mvxjson.c` → `libmvxext_json`;
`JSONENCODE`/`JSONDECODE` exports). This directory is the **portable BASIC**
codec for hosts without that intrinsic (UniData first) — so mvx-lang/json is one
cross-platform package: native where it can be, BASIC where it must.

- **`JSONDECODE(json, spec)`** — decodes per a `MAPFIELD` spec: flat
  `"key":"value"` scalars, and one **association** (an array of flat objects
  under the assoc key) into parallel multivalued attributes (`R<pos,i>`).
- **`JSONENCODE(rec, spec)`** — the inverse: an MV record → a JSON object;
  single-valued fields become `"key":value`, associations become
  `"key":[{…},…]` over the parallel multivalues. Keys lowercased; NUMERIC raw,
  DATE/TIME/TEXT quoted, empty → `null`/`""`; strings escape `"`,`\`, controls,
  and non-ASCII as `\u00XX`. Byte-for-byte the native encoder's output.

Both are enough for the common shapes (package metadata, the registry's
`/search`, dict-mapped records); the native mvx codec does the general case.
The spec is built with **`MAPFIELD`**, which lives in the separate
[`mapfield`](../../mapfield) package (this package depends on it) — the reusable
projection layer shared with future `yaml`/`xml` decoders.

## Robustness / roadmap

This BASIC decoder reads a value as the text between the first quote pair after a
key (no escape/nesting/number handling) — fine for the registry's responses, not
the general case. A more robust udt decoder is tracked separately: UniData's
native **UDO** JSON API, or a bundled **C parser via CallC**. See the json UDO/
CallC issue on mvx-lang/mvx.

## Note — MVPKG bundles its own copy

The MultiValue package manager (mvx-lang/mv_package) can't *depend* on this
package to reach the registry, so it ships an equivalent seam of its own
(`udt/JSONDECODE`, plus `MAPFIELD`) for bootstrap — the same relationship as its
bundled `CMD.BP` vs the `cmd` package. This port is the canonical source; keep
the two in step. (MVPKG only decodes, so it bundles `JSONDECODE` alone.)
