# mod-content-manager

AzerothCore module for discovering and validating EPF content packages, persistently
selecting packages, and building cumulative WoW 3.3.5a realm MPQs with bundled StormLib.

## Commands

| Command | Behavior |
| --- | --- |
| `.content status` | Shows whether Content Manager is enabled and its configured directories. |
| `.content scan` | Discovers EPFs, validates them, and shows persistent package selection, including missing installed sources. |
| `.content install <package-key>` | Validates exactly one discovered EPF with this key and persistently selects its version for future builds. |
| `.content uninstall <package-key>` | Removes the package selection; preserves the EPF and existing builds. |
| `.content stage <package-key>` | Stages one package and builds a separate development test MPQ, without installing it or creating a build record. |
| `.content build` | Creates a new cumulative MPQ from all INSTALLED packages, hashes it, records STAGED, then cleans its workspace. No argument is required. |
| `.content build list` | Lists completed realm builds newest first, with state, filename, package/file counts and SHA256. |
| `.content activate <build-number>` | Verifies an existing artifact and its SHA256, then selects it as ACTIVE. Also supports rollback. |

Activation requires an administrator session or the server console. Handled Content
Manager errors print their explanation without appending generic command usage.

## Package selection

- **AVAILABLE**: a valid EPF currently discovered.
- **INSTALLED**: a package persistently selected for inclusion in future realm builds.

The world database's `content_manager_package` table stores installed package keys,
names, versions, providers, source paths and installation times. Discovery supplies
available packages; it does not automatically install anything.

Installation requires exactly one discovered EPF declaring the package key. Duplicate
keys are reported with their conflicting sources. Reinstalling an already installed
key preserves its metadata and installation time. Uninstalling removes selection
even when the source is missing, without deleting the original EPF.

Scan shows both installed and available versions when they differ. It also shows
installed packages whose EPFs are missing or unavailable. Changing package selection
does not rebuild, activate, or publish a patch. The next build uses the selected set.
Package keys are case-sensitive, limited to 191 UTF-8 bytes, and cannot contain NUL
or end in a space. Registry writes are synchronous and verified by reading back.

## Current world schema

The module has one schema file:

`data/sql/db-world/base/content_manager_schema.sql`

AzerothCore's module database updater discovers SQL beneath `data/sql/db-world`.
Apply the schema through that updater before using the module, then reconfigure and
rebuild AzerothCore to include the module sources and bundled StormLib. OpenSSL's
crypto library, already required by AzerothCore, provides in-process SHA256.

The schema creates:

- `content_manager_package`: the working persistent package registry.
- `content_manager_build`: build number, realm name, filename, package/file counts,
  state, SHA256, and creation timestamp.
- `content_manager_build_lock`: a singleton InnoDB row serializing activation
  transactions across worldserver processes sharing the world database.

Build state is `VARCHAR(16)` with default `STAGED`. SHA256 is `CHAR(64) NOT NULL`
with no default. The application requires a lowercase 64-character hexadecimal
digest before recording any build. Invalid stored digests cause a registry error;
they are never displayed as successful builds or accepted for activation.

`CREATE TABLE IF NOT EXISTS` and the idempotent lock-row insert make the schema
rerunnable on fresh installations and installations already using this design.
They preserve package rows, build hashes and lifecycle states. They intentionally
do not reshape incompatible development tables. Before first use on an older
development database, stop worldserver and reset/recreate only the development
build tables as needed, then explicitly apply the current schema. Preserve
`content_manager_package`. Move conflicting development MPQs out of OutputDirectory
before restarting build numbering; the builder never overwrites an existing MPQ.
A manual table reset may require explicitly reapplying SQL if the updater has
already marked the schema file as applied. No runtime migration is performed.

## Cumulative builds

Every `.content build` reads all INSTALLED packages and rediscovers their original
EPFs. Each key must have exactly one valid source whose version matches the installed
record. Missing sources, duplicate keys, invalid EPFs, or version differences refuse
the build. Recorded source paths do not substitute for discovery. There are no
automatic upgrades or background workers.

The build creates a fresh workspace beneath `ContentManager.WorkDirectory`, named
`build-<number>-<unique-suffix>`. Only manifest-declared files are extracted. Earlier
workspaces and test MPQs are never used as build inputs.

All target paths are checked together before staging. Comparison is case-insensitive
and normalizes slash styles. Duplicate targets, including DBC files, and file/directory
conflicts fail with both package keys. Paths reject traversal, rooted paths, Windows
drive/stream/device aliases, trailing dots/spaces, and symlinks. There is no DBC row
merging or dynamic ID allocation.

Bundled StormLib creates the cumulative MPQ at:

```text
<ContentManager.OutputDirectory>/<Realm>-Content-000001.mpq
```

The realm name comes from AzerothCore's current realm and is sanitized for filenames;
no realm is hard-coded. ASCII letters, digits and underscores are retained, other
runs become hyphens, and an empty sanitized name becomes `Realm`. The original realm
name is stored in the database. Build numbers are padded to at least six digits and
continue from the highest recorded number after restarts.

After the MPQ is complete and closed, Content Manager calculates its SHA256 and
records the build as STAGED. Output includes the MPQ path, package/file counts,
SHA256, state and recording result. Only then does successful workspace cleanup run.
Building another MPQ leaves the current ACTIVE build unchanged.

### Failure handling

Only one cumulative build runs at a time within a worldserver. A candidate output
file that already exists is preserved and causes failure. Do not delete build-history
rows if their numbering history must be retained.

Validation errors occur before cumulative extraction. Staging, MPQ, hashing or database
recording failures preserve useful workspace/output data and report an error. Hashing
failure never inserts a successful STAGED record. A preserved unrecorded MPQ may block
reuse of its filename; inspect the failure and explicitly move that attempt aside
before retrying. No other successful builds are deleted or overwritten.

Cleanup only removes the successfully built workspace after verifying it is strictly
beneath WorkDirectory and does not traverse symlinks. Cleanup failure is a warning:
the valid MPQ and build record remain. Filesystem removal failure can leave a partially
cleaned workspace. Original EPFs and package installation records remain intact.

## Build lifecycle

- **STAGED**: a successfully built and hashed cumulative MPQ awaiting approval.
- **ACTIVE**: an administrator-approved realm content build.
- **SUPERSEDED**: a previous build retained and available for rollback.

Before activation, Content Manager loads the build, verifies that its artifact exists
in OutputDirectory, recalculates SHA256, and requires a match with the stored digest.
Missing, empty, non-regular, symlinked or modified artifacts refuse activation before
any state writes. Unsafe stored filenames are refused as well.

Successful activation runs in an InnoDB transaction: the current ACTIVE build becomes
SUPERSEDED and the selected build becomes ACTIVE. A singleton lock row serializes
module activation transactions across processes. There is one active selection per
world database. Selecting an already ACTIVE build still verifies its artifact and
hash, then reports that it is active without an unnecessary write.

A SUPERSEDED build can become ACTIVE again:

```text
.content build
.content build list
.content activate 1
```

After activating a newer build, `.content activate 1` selects build 1 again and
supersedes the current selection. Activation never rebuilds an MPQ or changes package
INSTALLED state. STAGED and SUPERSEDED artifacts are retained; there is no automatic
activation or old-build garbage collection.

Keep OutputDirectory administrator-owned and artifacts immutable during and after
hashing/activation. Hashing detects read failures and size/timestamp changes during
reading; it does not lock out external filesystem writers. Synchronous database
writes are read back because the core API does not return success. Inspect the SQL
log when an operation cannot be verified. Do not edit lifecycle rows while commands run.

Activation currently changes **Content Manager state only**. It does not copy MPQs
to a web-served directory, update `mod_realm_config_patch`, publish `realm.conf`, or
notify Portalkeeper. These are future integration work.

## Development test MPQ

`.content stage <package-key>` validates and extracts one EPF into
`<ContentManager.WorkDirectory>/<package-key>/`, then builds
`<ContentManager.OutputDirectory>/<package-key>-test.mpq`. Installation is not required.
It neither creates a cumulative build record nor changes the active build.

Successful staging cleanup checks that the returned staging directory is strictly
beneath the work directory and rejects symlink paths. Failure preserves useful
troubleshooting data. Existing MPQ files are never overwritten.
