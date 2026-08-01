# publish-source

Package the checked-out repository as the MV package registry's **source
artifact** and attach it to the release — the identical `source` job every MV
package's `release.yml` used to copy-paste.

It tars the working tree (minus `.git`/`.github` and any caller-named paths),
checksums it, and publishes `<base>-<version>-source.tar.gz` (base = the package
name with `/` → `_`) as a GitHub release asset, which the package registry then
indexes. The registry hosts nothing; it points at this asset.

## Usage

```yaml
jobs:
  source:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v5
      - uses: mvx-lang/mvx/.github/actions/publish-source@main
        with:
          package: mvx-lang/git          # base -> mvx-lang_git
          title: git                      # release named "git <tag>"
          body: "mv_git — git for hash-file records. Indexed by the package registry."
          extra-exclude: docs             # optional: a docs submodule, etc.
```

Run it on a version-tag trigger (`on: push: tags: ['[0-9]+.[0-9]+*']`) with
`permissions: contents: write`. Per-arch **binary** jobs (which are
package-specific — libgit2, ncurses, a self-hosted UniData runner, …) stay in
the package's own workflow and publish to the same release.

## Inputs

| input | required | description |
|-------|----------|-------------|
| `package` | yes | Package name, e.g. `mvx-lang/git`; the artifact base is this with `/` → `_`. |
| `title` | yes | Release display-name prefix; the release is named `<title> <tag>`. |
| `body` | yes | Release notes body. |
| `extra-exclude` | no | Space-separated extra top-level paths to leave out of the tar (`.git`/`.github` are always excluded). |
| `token` | no | GitHub token for the release (default `github.token`). |

## Output

- `tarball` — the source tarball filename.
