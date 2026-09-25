# Generic managed server resources

Milestone 2 extends the retained allocation and server-apply architecture with three generic namespaces:

- `creature-template.id`
- `gameobject-template.id`
- `creature-spawn.guid`

All are package/symbol scoped, deterministically allocated, collision checked, and permanently retained. Uninstalling or omitting a declaration from a later build does not release its lease.

Schema 2 and 3 EPFs may use the optional top-level arrays `creatureTemplates`, `gameobjectTemplates`, and `creatureSpawns`. No EPF schema bump is needed because these are optional typed descriptors and older schemas retain their existing meaning. Schema 1 rejects them.

Template declarations identify a stock `copyFrom` donor and a bounded `overrides` object. Descriptor v1 copies a reviewed set of runtime-relevant fields rather than exposing the full AzerothCore table. The creature-template descriptor contains `name`, `subname`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `rank`, `dmgschool`, `BaseAttackTime`, `RangeAttackTime`, `unit_class`, `unit_flags`, `type`, `type_flags`, `RegenHealth`, `flags_extra`, `AIName`, and `ScriptName`. Creature overrides support `name`, `subname`, `minLevel`, `maxLevel`, `faction`, `npcFlags`, `unitFlags`, `typeFlags`, `aiName`, and `scriptName`. Gameobject overrides support `name`, `type`, `displayId`, `size`, `aiName`, and `scriptName`. Donors must exist when the build is composed.

Permanent creature spawns use a package-local symbolic creature reference. Their GUID is allocator-owned; EPFs provide only the logical spawn symbol and bounded placement/respawn fields. Runtime or temporary spawns are not Content Manager resources.

`vendorRows` remains backward compatible with numeric `creatureEntry`. It may instead use `"creature":{"symbol":"..."}` for a package-local managed creature template. In that form the template descriptor owns the final vendor NPC flag and the vendor relationship does not independently mutate that managed field.

Server bundle/parity format 6 carries canonical managed-server representations. Explicit server apply validates leases, donors, collisions, ownership and current snapshots, then applies templates, vendor relationships, and spawns in one guarded InnoDB transaction. `content_manager_server_resource_owner` records exact provenance. Any guard or SQL failure rolls back the complete apply and leaves the build `STAGED`.

Runtime modules may vendor `src/api/ContentResourceApiV1.h`, discover its provider through the existing `WorldScript` RTTI pattern, and call `ResolveResource(package, symbol, kind, ...)`. Resolution succeeds only when the declaring artifact is the current `ACTIVE` build, its server state is `APPLIED`, parity and database state verify, and the retained lease agrees. A dormant lease is never an activation signal.

Before using this feature, apply `data/sql/db-world/updates/2026_09_25_06_content_managed_server_resources.sql` through the normal module SQL process. The migration only creates provenance storage; it allocates no IDs and changes no realm content.
