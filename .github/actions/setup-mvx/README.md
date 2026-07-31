# setup-mvx

Provision a **published** mvx toolchain onto a CI runner — the latest release
or a pinned version — so a workflow can compile BASIC and build mvx packages
without recompiling the compiler. It downloads the release tarball this repo
publishes on every `v*` tag (`mvx-lang-<ver>-linux-<arch>.tar.gz`, amd64/arm64),
installs the runtime dependencies, and puts `mvx` / `mvx-basic` on `PATH`.

Think of it as the mvx analogue of the `udt-builder` runner: any runner plus
this action is an mvx-capable runner at the version you ask for.

## Usage

```yaml
jobs:
  build:
    runs-on: ubuntu-24.04           # the tarball is built on ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: mvx-lang/mvx/.github/actions/setup-mvx@main
        with:
          version: latest           # or a pinned "0.1.0" / "v0.1.0"
      - run: mvx-basic --version
```

## Inputs

| Input         | Default            | Description                                        |
|---------------|--------------------|----------------------------------------------------|
| `version`     | `latest`           | `latest`, or a specific `X.Y.Z` / `vX.Y.Z`.        |
| `install-dir` | `/opt/mvx`         | Where to unpack the toolchain.                     |
| `token`       | `${{ github.token }}` | Token for the release API and asset download.   |

## Outputs

| Output    | Description                                   |
|-----------|-----------------------------------------------|
| `version` | The resolved version (no leading `v`).        |
| `home`    | Install prefix (`bin/`, `lib/`, `share/`).    |
| `bindir`  | The `bin/` directory (also prepended to PATH).|

## Notes

- Run on **ubuntu-24.04 or newer**. The published binaries are built on
  ubuntu-latest, so an older runner's glibc/libstdc++ will not satisfy them.
- For a fully hermetic job, skip this action and run in
  `container: ghcr.io/mvx-lang/mvx:<ver>` instead — the same build, pinned by
  image digest.
