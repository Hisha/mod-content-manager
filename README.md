# mod-content-manager
AzerothCore module for installing portable custom content packs, dynamically allocating realm-safe IDs, and generating synchronized server and WoW 3.3.5a client content.

## Test MPQ workflow

`.content stage <package-key>` validates the EPF, extracts its declared content into
`<ContentManager.WorkDirectory>/<package-key>/`, and builds
`<ContentManager.OutputDirectory>/<package-key>-test.mpq`.

After a successful MPQ build, the command removes only the package staging directory
returned by staging, after checking that it is strictly beneath the configured work
directory. The original EPF, work directory, other packages, and generated MPQ are
preserved. Cleanup through symlink paths is refused.

Staging or MPQ build failures leave existing staging data available for troubleshooting.
If the MPQ succeeds but cleanup fails, the command reports a cleanup warning and keeps
the valid MPQ. A removal failure may leave a partially cleaned staging directory.
Existing MPQ output files are never overwritten.

## Persistent package selection

- **AVAILABLE**: a valid EPF is currently discovered and its key is not installed.
- **INSTALLED**: the package is persistently selected in the world database for realm content builds.

| Command | Behavior |
| --- | --- |
| `.content scan` | Shows discovered EPFs, validation errors, and persistent package state. Also lists installed packages that are no longer discovered. |
| `.content install <package-key>` | Requires one matching discovered EPF, validates it again, and records its key, name, version, provider, absolute source path, and installation time. |
| `.content uninstall <package-key>` | Removes the installation record, even if the EPF is missing. Does not delete the EPF. |
| `.content build` | Reconstructs all installed content from the current EPFs, writes one realm MPQ, records the completed build, and cleans its workspace. |
| `.content stage <package-key>` | Runs the existing development test: stage, build a test MPQ, then clean up successful staging. Installation is not required. |

Installing and uninstalling change only the desired package set. They do not stage
files, rebuild or publish a client patch, or change realm configuration. A future
content build will be required for selection changes to affect clients.

Discovery never automatically installs a package. Duplicate manifest keys cause
installation to fail with the conflicting paths and providers. Reinstalling an
already installed key leaves its metadata and installation time unchanged.

When a discovered version differs from the stored version, scan displays both
installed and available versions. It does not update the stored selection.
Missing source EPFs are shown as `INSTALLED - SOURCE MISSING`, including when the
provider module has been removed. Their database rows are retained until explicitly
uninstalled. An inaccessible or non-regular source is reported as unavailable.

### Database setup

The module owns `content_manager_package` in the world database. Each row represents
an installed package; available packages come from discovery and are not stored.
The table has no package/build-state column. Apply module world SQL updates through
the normal AzerothCore database update mechanism before using registry commands:

- Fresh schema: `data/sql/db-world/base/content_manager.sql`
- Existing deployments: `data/sql/db-world/updates/2026_09_15_00_package_installation_registry.sql`

Both files use `CREATE TABLE IF NOT EXISTS` and can be rerun without changing existing
installation records. Reconfigure/rebuild the module to pick up the new registry source.
Registry failures are reported as errors rather than treating packages as available
or reporting an unverified installation as successful. Writes are synchronous and
verified by reading back the database state. Package keys are case-sensitive, limited
to 191 UTF-8 bytes, and may not contain NUL or end in a space. Metadata byte limits
are checked before writing to prevent truncation under permissive SQL settings.


## Cumulative realm builds

`.content build` reads the persistent **INSTALLED** package set and creates one
cumulative MPQ. Each selected key must have exactly one currently discovered,
valid EPF with the same version as the installed record. Missing sources,
duplicate keys, or version changes refuse the build and identify the package.
Sources are resolved through current discovery; the recorded installation path is
not used as a substitute for a missing discovered source.

The relationship is:

- **AVAILABLE**: a valid EPF is discovered, but its key has not been installed.
- **INSTALLED**: the administrator selected the package for the desired realm content set.
- **BUILD**: a completed cumulative MPQ generated from a snapshot of all installed packages.

Every build starts in a newly created directory beneath WorkDirectory, named
`build-<number>-<unique-suffix>`. Only manifest-declared files are extracted directly
into this workspace. Old package staging directories and earlier failed build
workspaces are never sources for a new build. Original EPFs remain authoritative.

All targets are checked together before staging. Comparison is case-insensitive,
and both slash styles map to the same archive path. Duplicate targets, including
DBC files, and file/directory conflicts fail with both package keys. There is no
DBC row merging. Paths use portable ASCII characters; traversal, rooted paths,
Windows drive/stream/device aliases, trailing dots/spaces, symlinks, and ZIP
symlink/device entries are refused. This validation also protects `.content stage`.

The filename uses AzerothCore's current realm name, sanitized for a filesystem:

    <ContentManager.OutputDirectory>/<Realm>-Content-000001.mpq

ASCII letters, digits and underscores are retained; other runs become hyphens.
An otherwise empty sanitized name becomes `Realm`. Numbers are padded to at least
six digits. The original realm name is kept in the database record.

The command runs synchronously, reporting packages, file counts, workspace and
output paths. It uses the existing StormLib MPQ v1 builder. It does not activate or
publish the result, copy it to a web directory, or change realm configuration.
Keep EPFs and the owned workspace unchanged while a build is running.

### Build records and failures

Apply `data/sql/db-world/updates/2026_09_15_01_cumulative_content_build.sql` through
the normal module world database updater. Fresh installations also have
`data/sql/db-world/base/content_manager_build.sql`. Both use
`CREATE TABLE IF NOT EXISTS`; rerunning them preserves completed build records.

`content_manager_build` stores build number, realm name, filename, package count,
file count, and completion timestamp. There is no build-state column. The next
candidate is one above the highest recorded number, so successful numbering
continues after worldserver restarts. Do not delete build-history rows if that
numbering history must be retained.

No build row is inserted until MPQ creation succeeds. Only one build runs at a time
within a worldserver. A candidate output that already exists is preserved and the
command fails with its path; it is never overwritten. Failed attempts do not
register successful builds or prevent retrying with a new clean workspace.

- Validation/collision failures occur before cumulative extraction.
- Staging or MPQ failures leave the newly owned workspace for troubleshooting and
  do not insert a completed-build row.
- If the MPQ succeeds but database recording cannot be verified, the MPQ and workspace
  are preserved and the command reports failure. Inspect the database and SQL log
  before reconciling that artifact; a retry may refuse its existing filename.
- After verified recording, workspace cleanup checks that the owned path is strictly
  below WorkDirectory. Cleanup failure is a warning: the valid MPQ and successful
  build record remain. Removal errors may leave a partially cleaned workspace.

Original EPFs, installed package records, other package workspaces, and generated
MPQs are preserved. `.content stage <package-key>` remains a separate one-package
development command; it neither requires installation nor inserts a build record.
