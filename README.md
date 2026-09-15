# mod-content-manager

AzerothCore module for discovering and validating EPF content packages, persistently
selecting packages, building cumulative WoW 3.3.5a realm MPQs with bundled StormLib, and publishing
approved versioned artifacts to a filesystem directory.

## Commands

| Command | Behavior |
| --- | --- |
| `.content status` | Shows whether Content Manager is enabled and its configured directories, including Publish Directory. |
| `.content scan` | Discovers EPFs, validates them, and shows persistent package selection, including missing installed sources. |
| `.content install <package-key>` | Validates exactly one discovered EPF with this key and persistently selects its version for future builds. |
| `.content uninstall <package-key>` | Removes the package selection; preserves the EPF and existing builds. |
| `.content stage <package-key>` | Stages one package and builds a separate development test MPQ, without installing it or creating a build record. |
| `.content build` | Creates a new cumulative MPQ from all INSTALLED packages, hashes it, records STAGED, then cleans its workspace. No argument is required. |
| `.content build list` | Lists completed realm builds newest first, with state, filename, package/file counts and SHA256. |
| `.content activate <build-number>` | Verifies and publishes the versioned artifact, verifies the published SHA256, then selects it as ACTIVE. Also supports rollback. |

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

Only after successful publication and final verification does activation run in an InnoDB transaction: the current ACTIVE build becomes
SUPERSEDED and the selected build becomes ACTIVE. A singleton lock row serializes
module activation transactions across processes. There is one active selection per
world database. Selecting an already ACTIVE build still verifies its source and ensures a matching
published copy exists, repairing a missing or corrupt copy if needed. It then reports
that it is active without an unnecessary database write.

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

## Filesystem publication

Content Manager owns this sequence:

```text
EPF -> cumulative MPQ -> SHA256 -> build lifecycle -> published filesystem artifact
```

It discovers EPFs, manages installed packages, builds and hashes cumulative realm
MPQs, and publishes approved versioned artifacts. Its responsibility ends at the
published filesystem artifact. It does not determine how that directory is exposed
publicly, what URL clients use, what client filename is used, or how realm
configuration advertises the patch. Those decisions belong to an independent
configuration/distribution system consuming the ACTIVE build metadata. No such
system is required for Content Manager to function.

### Configuration

```ini
ContentManager.OutputDirectory = "./patches"
ContentManager.PublishDirectory = "./published-content"
```

`PublishDirectory` is a filesystem path only. The default is `./published-content`;
relative paths resolve from worldserver's working directory. Use an absolute path
when desired. The module creates the directory during activation if necessary;
worldserver needs permission to create directories, copy files, and rename files
there. It is shown by `.content status` as `Publish Directory: <path>`.

PublishDirectory and OutputDirectory must be separate and non-overlapping. Keep
private output outside any publicly exposed tree. `.content build` never publishes,
so a STAGED build remains private until activation is explicitly requested. Keep
both directories administrator-owned, without symlink components, and do not modify
artifacts while commands run. Published files retain the source file permissions;
grant the intended distribution process read access through normal filesystem policy.

### Activation and rollback

Activation first verifies the source in OutputDirectory against the stored SHA256.
It preserves the exact versioned filename, for example:

```text
./patches/Realm-Content-000001.mpq
    -> ./published-content/Realm-Content-000001.mpq
```

If the final published file already has the expected SHA256, it is reused without
rewriting it. A corrupt regular file is safely replaced from the verified source.
Symlinks and non-regular destinations are refused.

For a new or replacement copy, Content Manager reserves a unique, owner-only
`.content-publish-*` staging directory inside PublishDirectory, copies to its
`artifact.tmp`, and verifies that copy's SHA256. It then promotes the completed file
with a same-filesystem rename and verifies the final published artifact again.
Readers of the final versioned filename never see a partial copy. Filesystem
operations are in-process; no shell copy, rename or hash utilities are invoked.
The temporary directory must not be served by the distribution system; it is
owner-only, and the final artifact becomes visible only at promotion.

Only after those checks does the database transaction select ACTIVE and supersede
the former selection. Source, copy, hashing, directory, promotion or final verification
failure leaves lifecycle states unchanged and reports the cause. Temporary files
are cleaned when safe; cleanup errors report the path. Sources and unrelated
published files are never deleted. Atomic replacement of an existing file is supported
on the deployment's Linux filesystem; platforms that reject replacement by rename
fail safely without first removing the destination. This is atomic visibility,
not a guarantee of persistence through sudden power loss.

Rollback uses the same workflow, and older published versions are retained. There
is no garbage collection. Concurrent publication of the same filename from separate
processes can produce a safe verification refusal if the file changes during hashing;
retry the activation after competing work finishes. Database state transitions remain
serialized by the existing activation lock.

Filesystem publication and the database transaction are separate operations. If
publication succeeds but database activation fails, the verified published artifact
can remain on disk without becoming ACTIVE. A failed activation never intentionally
changes the previous ACTIVE selection. Retry after inspecting the SQL error log;
the matching published copy is reused. The ACTIVE row is authoritative for consumers,
not mere presence of a filename in the publication directory.

A successful activation reports the verified build number, full published path,
SHA256 and ACTIVE status. The build metadata contract remains `build_number`,
`realm_name`, `filename`, `sha256`, `state`, `package_count`, `file_count`, and
`created_at`. An independent consumer can query `state = 'ACTIVE'` to discover the
filename, hash and realm without any consumer-specific schema or dependency here.

### Publication checks

Run `python3 tests/test_publication.py` with a C++17 compiler and OpenSSL development
files. Tests cover initial publication, verified reuse without rewriting, corrupt
and empty destination repair, source mismatch, missing sources, unsafe destinations,
overlapping directories, retained rollback artifacts, concurrent attempts, and
partial-copy failure cleanup (POSIX).

On a development worldserver, also verify that a forced publication failure leaves
the current ACTIVE row unchanged, that an already ACTIVE build repairs its published
copy, and that activating a SUPERSEDED build preserves other published versions.

## Development test MPQ

`.content stage <package-key>` validates and extracts one EPF into
`<ContentManager.WorkDirectory>/<package-key>/`, then builds
`<ContentManager.OutputDirectory>/<package-key>-test.mpq`. Installation is not required.
It neither creates a cumulative build record nor changes the active build.

Successful staging cleanup checks that the returned staging directory is strictly
beneath the work directory and rejects symlink paths. Failure preserves useful
troubleshooting data. Existing MPQ files are never overwritten.
