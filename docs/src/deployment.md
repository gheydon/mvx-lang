# Deployment and the Daemon

## Single host: embedded

The default. Each account directory carries its own
`mvxdata.lmdb` environment; concurrent sessions on one host share it
safely (LMDB single-writer, many readers). Directory files are plain
directories. Nothing to operate.

## Multi host: mvx-lmdbd

LMDB must never live on a network filesystem. For more than one host,
`mvx-lmdbd` owns the environment exclusively and serialises access:

```sh
mvx-lmdbd -d /var/mvx/data -s /run/mvx-lmdbd.sock     # or -p 4700 for TCP
```

Clients switch with one variable — the promised config swap:

```sh
export MVXDAEMON=/run/mvx-lmdbd.sock             # or host:4700
mvx -a /path/to/account
```

With only `$MVXDAEMON` set, every LMDB-backed file — data,
dictionaries, indexes, the VOC — lives in the daemon; directory files
(BP source) stay local. No application change, no relink: the
`lmdbnet` driver presents the identical contract over the wire.

## Choosing a backend per file

A file's backend is named at creation:

```
> CREATE-FILE ORDERS                          local LMDB (default)
> CREATE-FILE ARCHIVE DIR                      directory (git-native source)
> CREATE-FILE SHARED USING lmdbnet /run/mvx-lmdbd.sock   networked LMDB
> CREATE-FILE HOT USING lmdbnet               networked, default $MVXDAEMON
```

`USING <driver> {connection}` binds the file to a storage driver —
`lmdbnet` today, and `postgres`/`mongo` as they arrive — recording it
in the account's `BINDINGS` record and creating the file through that
driver. `DELETE-FILE` removes the file and its binding; `LISTF` shows
each file's backend by name (`lmdb`, `directory`, `lmdbnet`, ...).
Different files may use different drivers and connections, so one
account can mix local LMDB, networked files on several daemons,
directory files, and (later) SQL or document stores — and one program
reads them all through ordinary `OPEN`/`READ`/`WRITE`.

The binding is a plain, hand-editable record: `SPEC driver {params}`
per line (`*` as the spec binds every LMDB file; `params` is the
driver's connection string, defaulting to `$MVXDAEMON` for lmdbnet).
With no `BINDINGS` record, bare `$MVXDAEMON` binds the whole account
to `lmdbnet` — the simple all-networked deployment. Binding is
resolution only; existing data does not move when it changes.

To actually move a file to a different backend, use **`CONVERT-FILE`**:

```
> CONVERT-FILE ORDERS dir                         hash file -> directory file
> CONVERT-FILE ORDERS lmdb                        directory file -> hash file
> CONVERT-FILE ORDERS USING lmdbnet /run/mvx.sock   local -> networked
```

`CONVERT-FILE file newtype {connection}` re-keys the file's records —
and its dictionary — into the new backend verbatim, so a hash file
round-trips to a directory file and back without loss, and the `%FILE%`
control record is restamped to the new type. It is the per-file
companion to mvx-convert-acct (which rebuilds a whole account).

What the daemon guarantees:

- **Single lock authority.** `READU` locks are granted by the daemon
  and **leased to the connection**: a client that dies without
  `RELEASE` loses its locks the moment the connection drops. No
  orphaned locks from killed pods.
- **Index atomicity.** A record write and its index updates commit in
  one daemon-side transaction.
- **Snapshot selects.** `SELECT` materialises the id list before
  sending; a slow client never pins a read transaction.

Costs, accepted deliberately: the daemon is a single point of
failure, the single-writer throughput ceiling remains, and
replication/HA is not provided. Protocol integers are host-order —
keep daemon and clients on the same architecture for now.

## Multiple accounts on one daemon

Several accounts can share one `mvx-lmdbd`, isolated by **namespace**:

- **One environment per namespace.** Every request names a namespace
  (≈ a Pick account), and the daemon keeps a separate LMDB environment
  for each under `<datadir>/<namespace>/`. Same-named files in different
  namespaces are fully isolated — two accounts can each have their own
  private `ORDERS` — and each namespace gets its own budget of files
  rather than sharing one global cap.
- **The namespace defaults to the account name.** A file bound to a
  daemon uses the account's own namespace (the basename of its account
  directory) unless the binding says otherwise. So pointing two accounts
  at one daemon isolates them automatically.
- **Sharing is explicit — the Q-pointer analog.** To share a file, name
  its home namespace in the `BINDINGS` line: `SPEC lmdbnet addr namespace`.
  Any account that names `SALES` reads `SALES`'s data:

  ```
  ORDERS     lmdbnet  mvxdb-a:4300  SALES
  CUSTOMERS  lmdbnet  mvxdb-a:4300  SALES
  ```

  Omit the namespace and the account gets its own. `$MVXDAEMON` binds the
  whole account to a daemon in the account's own namespace.
- **Locking is coordinated, and namespace-scoped.** The daemon is the
  single lock authority; a `READU` from one account blocks another only
  when they address the *same* namespace + file + record. Locks are
  leased to the connection (dropped on disconnect).
- **Dictionaries are per-account views.** A remote data file's dictionary
  (and its `%FILE%`/`%INDEXES%` control records) lives with the account
  that binds it, so different accounts can hold different column
  definitions over the same shared data. Index *data* is shared in the
  namespace; index *definitions* are local.
- **No cross-daemon atomicity.** An account can bind files on several
  daemons at once, but each daemon is its own lock and transaction
  authority — a unit of work spanning two daemons cannot be one
  transaction.

Still open (see the investigation, [#5](https://github.com/mvx-lang/mvx/issues/5)):
a slow client can still stall others
([#10](https://github.com/mvx-lang/mvx/issues/10)).

*Migration note:* pre-namespace data written by an earlier daemon lived
in a single `<datadir>/mvxdata.lmdb`; it is not read by the namespaced
daemon (which looks under `<datadir>/<namespace>/`). There is no
production data at this stage, so no migration path is provided.

### Authenticating a namespace

By default the daemon runs **open** — any client that reaches the port
can access any namespace. To require a token, provision each namespace on
the daemon host with **`mvx-lmdbd-admin`**, a standalone tool (no MVX
runtime needed):

```sh
mvx-lmdbd-admin -d /data create-account SALES     # prints the token once
```

This writes a salted hash of a generated token into the creds list
`/data/accounts` (mode `0600`) — the token itself is never stored. Once
the file exists the daemon **requires authentication** for every
namespace: a connection must present a matching token before any
operation. Give the token to the client through its credential store:

```
SET-CREDENTIAL lmdbnet mvxdb:4300 SALES token=<token>
```
```
BINDINGS:  ORDERS  lmdbnet  mvxdb:4300  SALES
```

The lmdbnet driver reads the token from `.mvx-private` and authenticates
the connection automatically; an account with no token, or the wrong one,
is denied. Provisioning is offline and local — access to the daemon's
data dir is the trust boundary, so there is no admin password on the
wire. `mvx-lmdbd-admin` also has `rotate`, `delete-account`, and
`list-accounts`. The daemon and this tool stay **Pick-agnostic**: a
namespace is just an opaque partition, authorised by a bearer token.

### Named connections

Rather than write the daemon host into every file's binding, define a
**named connection** once and reference it. The committed `BINDINGS`
names only the connection; the deployment-specific host, namespace, and
token live in the local (git-ignored) `.mvx-private/connections`:

```
> SET-CONNECTION salesdb driver=lmdbnet address=mvxdb:4300 namespace=SALES token=<token>
> CREATE-FILE ORDERS USING @salesdb
> CREATE-FILE CUSTOMERS USING @salesdb
```
```
BINDINGS:  ORDERS     @salesdb
           CUSTOMERS  @salesdb
```

Move the daemon to a new host and you change **one** field —
`SET-CONNECTION salesdb address=newhost:4300` — with no change to
`BINDINGS` and nothing to re-commit. The same profile carries the token,
so a connection is the single place a file's whole remote binding lives;
`LIST-CONNECTIONS` shows the profiles with secret fields masked. It is
also how a container injects a target — set `MVXCONN_SALESDB_ADDRESS` and
`MVXCONN_SALESDB_TOKEN` and no file is needed. The same mechanism will
carry Postgres and other backends (`driver=postgres address=… dbname=…
user=… password=…`). The older inline form (`CREATE-FILE … USING lmdbnet
<addr>`) still works.

## Credentials and secrets (`.mvx-private`)

Backends that authenticate — a networked-LMDB namespace token, a Postgres
username and password — need a secret to connect. Those secrets must
**not** live in git-committed account config (`BINDINGS`, `.mvx`, the
VOC): a clone or `BUILD` should provision an account without carrying its
credentials, with the operator supplying them out-of-band.

So secrets live in a per-account directory **`.mvx-private/`**, a single
`credentials` file, netrc/pgpass-style — created `0700`/`0600`, and
git-ignored. `BINDINGS` names only the non-secret reference (driver,
target, key); the runtime resolves the secret from the store.

```
> SET-CREDENTIAL lmdbnet mvxdb-a:4300 SALES token=abc123
> SET-CREDENTIAL postgres db:5432 mvx user=app password=s3cret
> LIST-CREDENTIALS                       values are masked
```

`.mvx-private/credentials` then holds one **field per line** — the first
three whitespace tokens are `driver target key`, and the rest of the line
is `field=value`, where the value runs to end-of-line and may contain
spaces and other characters (so an arbitrary secret survives verbatim):

```
lmdbnet  mvxdb-a:4300  SALES  token=abc123
postgres db:5432       mvx    user=app
postgres db:5432       mvx    password=p@ss w0rd :/@
```

An environment variable overrides the file so a container can inject a
secret without writing one — `MVXCRED_<DRIVER>_<KEY>_<FIELD>`,
upper-cased with non-alphanumerics as `_` (e.g. `MVXCRED_LMDBNET_SALES_TOKEN`).
Resolution is env override, then the file, then a clear error.

### Deploying secrets from CI / GitHub Actions

The store is plain text with an end-of-line value, so a deploy job can
pull secrets from GitHub Actions secrets and write them out. Two patterns:

**Whole file as one secret** — keep the entire `credentials` file content
in a single repository/environment secret and write it with a tight umask:

```yaml
- name: Write credentials
  run: |
    mkdir -p account/.mvx-private
    ( umask 077; printf '%s' "${{ secrets.MVX_CREDENTIALS }}" \
        > account/.mvx-private/credentials )
```

**Per-field env vars** — map individual secrets to the override variables
and write no file at all:

```yaml
env:
  MVXCRED_POSTGRES_MVX_PASSWORD: ${{ secrets.PG_PASSWORD }}
  MVXCRED_LMDBNET_SALES_TOKEN:   ${{ secrets.SALES_TOKEN }}
```

Either way the secret never lands in git; `.mvx-private/` stays local to
the deployed account.

Keep `.mvx-private/` out of version control — it is a dotfile, so MVX's
own account export already skips it; for an account kept in a plain git
repo, add it to `.gitignore` alongside `mvxdata.lmdb/`. The consuming
backends (networked-LMDB tokens, the Postgres driver) are tracked in
[#7](https://github.com/mvx-lang/mvx/issues/7),
[#9](https://github.com/mvx-lang/mvx/issues/9), and
[#11](https://github.com/mvx-lang/mvx/issues/11).

## Committing and cloning an account through git

Hash files are binary, so an account is never committed as-is. Git
tracks the **directory form** — each file as `NAME/` (one text file per
record) beside `NAME.DICT/` (its dictionary) — while a **live account is
hash files with no `.DICT`**. `mvx-convert-acct` moves between the two:

```
mvx-convert-acct <account>            directory form -> live hash files
mvx-convert-acct --export <account>   live hash files -> directory form
```

On **import**, every file is rebuilt as the backend its dictionary's
`%FILE%` control record names — a hash file for an `lmdb` file, a
directory file (keeping its `NAME.DICT`) for a `dir` file — then `BUILD`
catalogs `BP` and links packages. On **export**, each hash file is
written out as `NAME/` + `NAME.DICT/`, copying the dictionary verbatim
so its `%FILE%` still records the real backend. `%FILE%` is the file
definition, so a committed dictionary alone recreates its file.

You rarely call `mvx-convert-acct` directly — **`mvx-git` does it for
you**: it rebuilds the account after `clone`/`checkout`/`pull` and
exports it before `commit`/`add` (see [Version Control](git.md)). Run
`mvx-convert-acct` by hand only for a *plain* `git` checkout — the
bootstrap that turns mvx-lang's own `system` account and packages into
real accounts before `mvx-git` exists.

This is the stock-to-site delivery path — combined with record-level
branch, merge, and cherry-pick (see [Version Control](git.md)), a
customised site tracks stock upstream and feeds features back.

## The migration curve

```
embedded LMDB  ->  networked LMDB  ->  (heavier backends)
```

Each step is configuration, not surgery, because the driver contract
is the boundary. Migration can be per file: a directory file, an
embedded file and a daemon file coexist in one account.

## Privilege and containers

`$MVXPRIV` comes from the process environment — system-level
configuration that account data cannot write. In containers, run app
sessions stateless with the account on a volume or entirely behind
`mvx-lmdbd`, and grant `unrestricted` only where a shell escape is truly
needed.
