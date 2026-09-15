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
- **INSTALLED**: the package is persistently selected in the world database for future realm content builds.

| Command | Behavior |
| --- | --- |
| `.content scan` | Shows discovered EPFs, validation errors, and persistent package state. Also lists installed packages that are no longer discovered. |
| `.content install <package-key>` | Requires one matching discovered EPF, validates it again, and records its key, name, version, provider, absolute source path, and installation time. |
| `.content uninstall <package-key>` | Removes the installation record, even if the EPF is missing. Does not delete the EPF. |
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
