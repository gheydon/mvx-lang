# Containers

Three images ship MVX to Docker, built from `docker/` in the repo:

| image | what it is |
|---|---|
| `mvx` | the base system — compiler, shell, runtime, drivers, standard verbs |
| `mvx-lmdbd` | the networked LMDB daemon |
| `mvx-demo` | a ready-to-run account with the example programs cataloged |

Build all three (base first, since the others layer on it):

```sh
docker/build.sh                       # mvx-lang/mvx{,-lmdbd,-demo}:latest
REGISTRY=you TAG=0.1 docker/build.sh  # custom namespace / tag
PUSH=1 docker/build.sh                # build, then docker push each
```

Each has its own Dockerfile (`docker/Dockerfile.base`, `.lmdbd`,
`.demo`) if you prefer to build them one at a time. The daemon and demo
`FROM` the base via a `BASE` build-arg (default `mvx`), so a published
base can be layered on directly.

On a `v*` git tag, the `release` GitHub Actions workflow builds and
pushes all three images multi-arch (`linux/amd64` and `linux/arm64`) to
Docker Hub under `mvx-lang/`, alongside the native Linux binary
tarballs — see *Publishing*.

## `mvx` — the base system

A multi-stage build compiles MVX against LLVM and installs it to
`/usr/local`, then a slim runtime layer keeps the install plus a C
toolchain (`mvx-basic` shells out to `cc` to link programs). `mvx` is on
`PATH`, and it locates its runtime, drivers, and system account relative
to itself — no environment variables.

```sh
docker run --rm -it mvx                 # MVX shell in a scratch /work account
docker run --rm mvx mvx-basic -h        # or run any tool directly
docker run --rm -v "$PWD":/src mvx \
  mvx-basic /src/prog.b -o /src/prog     # compile a program from a bind mount
```

It defaults to the **developer** privilege tier so `BASIC`, `CATALOG`,
and `COMPILE()` work. Use it as the base for your own account images.

## `mvx-lmdbd` — the daemon

Serves LMDB-backed files to remote MVX processes over TCP. The database
lives on a volume; the port defaults to 4300.

```sh
docker run -d --name mvxdb -p 4300:4300 -v mvxdata:/data mvx-lmdbd
```

Point clients at it per file or per account:

```
CREATE-FILE ORDERS USING lmdbnet host.docker.internal:4300
```
```sh
docker run --rm -it -e MVXDAEMON=host.docker.internal:4300 mvx
```

## `mvx-demo` — the example account

Boots straight into a `/demo` account with the example programs
cataloged as verbs. Give it a TTY so the full-screen programs render:

```sh
docker run --rm -it mvx-demo
demo> SNAKE       # arrows steer; q or ESC quits
demo> FSDEMO      # the Midnight Commander-style full-screen demo
```

Both are ordinary cataloged BASIC — `LISTF`, `LIST BP`, or `VI BP SNAKE`
to look under the hood.

## Publishing

Releases are automated. Pushing a `v*` tag runs the `release` workflow,
which publishes the three images multi-arch to Docker Hub under
`mvx-lang/` and attaches per-architecture Linux binary tarballs to the
GitHub release:

```sh
git tag v0.1.0 && git push origin v0.1.0
```

The workflow needs two repository secrets — `DOCKERHUB_USERNAME` and
`DOCKERHUB_TOKEN` (a token with push rights to the `mvx-lang` Docker Hub
org). To publish by hand instead, `docker/build.sh` tags images
`${REGISTRY}/mvx{,-lmdbd,-demo}:${TAG}` (`REGISTRY` defaults to
`mvx-lang`); `docker login`, then run with `PUSH=1`.
