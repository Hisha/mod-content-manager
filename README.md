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
