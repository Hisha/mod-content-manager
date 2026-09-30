# Native world map DBCs (DungeonMap, DungeonMapChunk, WorldMapArea, WorldMapTransforms)

This release adds **native pre-Cataclysm instance map support** to mod-content-manager. It changes
**mod-content-manager only**. AzerothCore, the WoW client, and all client addons stay unchanged; no
addon is created, and no consumer EPF, stock baseline DBC, or generated MPQ is committed.

The client reads instance placement and map coverage exclusively from these four build-12340 DBCs.
Because the client has no server-side fallback, a mod that only defines creatures, objects, or spawns
does not make an instance visible on the world map or in the instance map. Declaring `worldMaps[]` is
what makes that content reachable.

## Exact build-12340 layouts

WDBC header: magic plus four little-endian uint32 values (record count, field count, record size,
string bytes). Fields whose client-side meaning is not established are named `fieldN` and are never
given invented semantics.

`DungeonMap.dbc` — 8 fields, 32-byte records, no strings:

| Word | Field |
|---:|---|
| 0 | ID (`worldmap.dungeon-map.id`) |
| 1 | MapID |
| 2 | Floor |
| 3–6 | field3 … field6 (float) |
| 7 | field7 |

`DungeonMapChunk.dbc` — 5 fields, 20-byte records, no strings:

| Word | Field |
|---:|---|
| 0 | ID (`worldmap.dungeon-map-chunk.id`) |
| 1 | MapID |
| 2 | field2 |
| 3 | DungeonMapID |
| 4 | field4 (float) |

`WorldMapArea.dbc` — 11 fields, 44-byte records, one string column:

| Word | Field |
|---:|---|
| 0 | ID (`worldmap.world-map-area.id`) |
| 1 | map_id |
| 2 | area_id |
| 3 | internal_name (string offset) |
| 4–7 | y1, y2, x1, x2 (float) |
| 8 | virtual_map_id (int32) |
| 9 | dungeonMap_id (int32) |
| 10 | parentMapID |

`WorldMapTransforms.dbc` — 10 fields, 40-byte records, no strings:

| Word | Field |
|---:|---|
| 0 | ID (`worldmap.world-map-transforms.id`) |
| 1 | MapID |
| 2–5 | RegionBottom, RegionRight, RegionTop, RegionLeft (float) |
| 6 | NewMapID |
| 7–8 | RegionOffset_X, RegionOffset_Y (float) |
| 9 | NewDungeonMapID |

The descriptors live in `src/DbcDescriptor.cpp` and are reachable through `WorldMapDbcTables()` and
`.content dbc inspect <table>`.

## Schema 2 and 3 declaration

`worldMaps[]` is a schema 2/3 top-level array. Schema 1 packages are rejected. The shape is
`worldMaps[] -> {mapId, transform (optional), areas[]}`, and every area owns `floors[]` and `chunks[]`:

```json
"worldMaps": [
  {
    "mapId": 36,
    "transform": {
      "id": 11,
      "regionBottom": -20000.0, "regionRight": -20000.0,
      "regionTop": 20000.0,     "regionLeft": 20000.0,
      "newMapId": 36, "regionOffsetX": 0.0, "regionOffsetY": 0.0,
      "newDungeonMapId": 167
    },
    "areas": [
      {
        "id": 756,
        "areaId": 1581,
        "internalName": "TheDeadmines",
        "y1": 1966.6666259765625, "y2": -3033.333251953125,
        "x1": 1133.333251953125, "x2": -2200.0,
        "virtualMapId": -1,
        "dungeonMapId": 0,
        "parentMapId": 0,
        "floors": [
          { "id": 166, "floor": 1, "field3": -796.6220092773438,
            "field4": -237.35800170898438, "field5": -337.5090026855469,
            "field6": 35.333499908447266, "field7": 39 },
          { "id": 167, "floor": 2, "field3": -1016.6199951171875,
            "field4": -517.3569946289062, "field5": -267.5090026855469,
            "field6": 65.33329772949219, "field7": 39 }
        ],
        "chunks": [
          { "id": 2521, "field2": 193, "dungeonMapId": 166, "field4": -10000.0 },
          { "id": 2524, "field2": 194, "dungeonMapId": 166, "field4": -10000.0 }
        ]
      }
    ]
  },
  {
    "mapId": 90,
    "areas": [
      {
        "id": 691,
        "areaId": 940,
        "internalName": "Gnomeregan",
        "y1": -4500.0, "y2": -4800.0, "x1": -5200.0, "x2": -4900.0,
        "virtualMapId": -1,
        "dungeonMapId": -1,
        "parentMapId": 0,
        "floors": [
          { "id": 900, "floor": 1, "field3": 0.0, "field4": 0.0,
            "field5": 0.0, "field6": 0.0, "field7": 39 }
        ],
        "chunks": [
          { "id": 5000, "field2": 7, "dungeonMapId": 900, "field4": -10000.0 }
        ]
      }
    ]
  }
]
```

The second entry above is a complete map with no `transform` key. It contributes a floor, a chunk and
an area, requests nothing from `WorldMapTransforms`, and leaves the composed transform table equal to
stock.

The `fieldN` keys mirror the descriptor exactly. They are not placeholders waiting to be renamed:
their client-side meaning is not established, so the module refuses to invent one. `mapId` is written
to every owned row's `MapID`/`map_id`, so it is not repeated per floor or chunk. Note that
`DungeonMapChunk.ID` is unrelated to the WDL tile it covers, and that chunk order is significant and
preserved exactly as declared.

## `transform` is optional

A `transform` is a world-map *override*, not part of a map's identity, and most stock and WDM maps
declare none. Karazhan in WDM Stable (map 532) is the clearest example: 17 floors, an area, and 86
chunks, with no `WorldMapTransforms` row. So `transform` is optional and defaults to *absent*, never
to a default row. Omitting it is the accurate way to say "this map has no override", and a map
without one is a complete, valid contribution.

When the key is absent the module does nothing on the author's behalf:

- No `WorldMapTransforms` request, fixed ID, lease, or parity hash is produced. `NewDungeonMapID` is
  never inferred, defaulted, or guessed from another map's row.
- No transform row is composed, so the composed `WorldMapTransforms.dbc` is the verified stock file
  byte for byte.
- `AreaWords()` and the fixed-ID planner are simply not invoked for that table.

A present `transform` is validated exactly as before, and in particular `newDungeonMapId` must still
name a floor declared in the same map. Relaxing the *presence* requirement does not relax any
validation of a transform that is actually declared.

## `areas[].dungeonMapId` is a reference, not an owned ID

`areas[].dungeonMapId` is a signed `int32` **reference** to a `DungeonMap` floor. It is not an ID this
package allocates, and it is written through to `WorldMapArea.dbc` word 9 unchanged. The stock and WDM
tables all rely on that:

| Declared value | Meaning in the client |
|---|---|
| `0` | no dungeon map (the common case) |
| `-1` | explicit "unset" sentinel |
| positive | a floor of *some* map, which need not be this one |

WDM Stable contains a live cross-map case: `WorldMapArea` 766 (Ahn'Qiraj, map 531) carries
`dungeonMapId = 2`, while `DungeonMap` 2 belongs to map 574. Rejecting that would make real content
unauthorable, so a reference that the package does not own is accepted and preserved byte for byte.

The one case still refused is a reference to a floor **this same package** owns under a *different*
`mapId`, because the client would resolve it as if it belonged to this map and silently paint the
wrong floor. A floor may still only be owned by one map in a package. `DungeonMapChunk.dungeonMapId`
keeps the stricter original rule: a chunk must belong to a floor of the map that declares it, which
holds for every stock and WDM chunk.

Validation is deliberately strict, because these tables are client-baked:

- Every object is closed: any key not listed above is rejected as unsupported/allocator-owned.
- All IDs are non-zero and unique within their table; all `mapId` values are non-zero.
- `virtualMapId` is **required and explicit** even when the intended value is `-1`, so a
  "no override" case can never be produced by silently omitting a field.
- `areas[].dungeonMapId` is closed as a set: `0`, `-1`, and out-of-range or non-integer values are
  still rejected, but any in-range floor reference is kept verbatim.
- Floor references that must resolve to this map are validated **map-wide** across all areas of one
  `worldMaps[]` entry, so `chunks[].dungeonMapId` and `transform.newDungeonMapId` must name a floor
  declared somewhere in the same map, not merely in the same area.
- Artwork declared for a world-map area must live under
  `Interface/WorldMap/<internalName>/`; a tile anywhere else is rejected.
- A schema 2/3 package that declares nothing at all (no content, no server, no world maps) is
  rejected, and staged builds require the staged manifest to equal the selected manifest.

## Fixed identities and durable ownership

There is no allocator for these rows: the client bakes a map's geometry and floor layout, so
reassigning an ID between builds would silently repaint an existing map. Every row therefore carries
an author-declared ID that is leased durably per table and resource kind:

| Table | Resource kind |
|---|---|
| `DungeonMap` | `worldmap.dungeon-map.id` |
| `DungeonMapChunk` | `worldmap.dungeon-map-chunk.id` |
| `WorldMapArea` | `worldmap.world-map-area.id` |
| `WorldMapTransforms` | `worldmap.world-map-transforms.id` |

`ContentResourceAllocator::PlanFixed()` reuses retained leases for the same
package/symbol/kind/identity, requires exact `baselineSha256` agreement, and rejects a declared ID
that is already occupied by stock or by another package's lease. Retired leases stay occupied: a
removed package's row ID is never handed to another owner, so IDs are effectively permanent. The
historical lease and composed file hash also stay in the parity artifact.

## Append-only composition and string-block safety

`src/WorldMapDbcComposer.cpp` composes each table from the verified stock baseline:

- Every stock record and every stock string byte is preserved byte for byte, in original order.
  Nothing rewrites, sorts, or removes a stock row.
- Package rows are appended after the stock rows, ordered by package key, then by declaration order
  within the manifest. This makes composition deterministic and keeps an area's chunk order exactly
  as authored (the client's Deadmines chunk order is not ID-sorted, and is preserved as declared).
- `WorldMapArea` string offsets are appended to the stock string block. Offset 0 must already be the
  empty string, so appending can never move an existing offset; a declared `internalName` is
  interned so repeated names share one appended copy.
- Only declared rows are appended. A map without a `transform` appends no `WorldMapTransforms` row,
  so a package that declares no transform at all reproduces the stock transform file exactly.
- Collision and read-back verification run before the build is accepted: a declared ID that already
  exists in the stock table, a mismatched record layout, or a composed file that does not reparse
  into the expected rows fails the build.

Verified stock 3.3.5a build-12340 baselines (the build refuses any other file):

| Table | Stock rows | SHA-256 |
|---|---:|---|
| `DungeonMap` | 55 | `aa31db35a2266694d8318c410712a18540d86469e3d6134eb648a1d9694f21f5` |
| `DungeonMapChunk` | 622 | `c9fda294ce565501518aa782957a5fe45d92147a79c34a5f29493194a8d318c3` |
| `WorldMapArea` | 108 | `90e1ec678c8226c76f4dd9f7904f05a4cb0a58c8b4d386719b24949bd55925bb` |
| `WorldMapTransforms` | 9 | `ba3f2a65c35ba0ab158f4a336c6195b0dcf4754d2e4f53fea5dbe5acf1ad479c` |

Baseline selection uses the same generic baseline registry as the other phases: register a validated
baseline on first use, then require exact matches, with `.content dbc review` / `.content dbc approve`
for deliberate replacement. No separate world-map SHA setting is required or supported.

## Parity artifact

The world-map tables are client-only, so they contribute no managed server rows. They are recorded in
parity **artifact format 8** as `worldMapDbcSha256`: a table-to-hash map covering exactly the tables
that were composed. Each world-map lease is emitted with its package, symbol, resource kind, value,
baseline hash, allocation policy version and DBC descriptor version, and `VerifyParity` requires
round-trip equality, exact hash/lease coverage, and lease-to-baseline provenance agreement.

## Deadmines acceptance fixture

`tests/fixtures/deadmines/manifest.json` is the proven minimal Deadmines contribution: map 36,
transform 11, area 756/1581, floors 166 and 167, 29 chunks, and the 24 client BLP tiles. Composing it
over the stock baselines reproduces the verified patch byte for byte:

| Table | Stock | Deadmines | Result |
|---|---:|---:|---|
| `DungeonMap` | 55 | 2 | 57 records |
| `DungeonMapChunk` | 622 | 29 | 651 records |
| `WorldMapArea` | 108 | 1 | 109 records, 1303 → 1316 string bytes (`"TheDeadmines\0"`) |
| `WorldMapTransforms` | 9 | 1 | 10 records |

The manifest is bare JSON so it can be reviewed as a diff. Because stock client data is not
committed, `build_epf.py` assembles the EPF from the manifest plus an artwork directory:

```bash
python3 tests/fixtures/deadmines/build_epf.py \
  --manifest tests/fixtures/deadmines/manifest.json \
  --artwork /path/to/Interface/WorldMap/TheDeadmines \
  --output /tmp/mod-deadmines-dungeon-map.epf
```

The script writes a deterministic, uncompressed archive with a fixed 1980 timestamp (so the same
inputs always produce the same bytes) and refuses to overwrite an existing file.

## Running the tests

`tests/world_map_tests.cpp` is wired into the canonical Phase 4 runner. The world-map unit is
skipped unless the stock, golden, and artwork directories are supplied:

```bash
python3 tests/run_phase4.py \
  --world-map-baseline-dir   /home/smithkt/git/WDM-patch/wdm-stock-dbc \
  --world-map-golden-dir     /home/smithkt/git/WDM-patch/minimal-deadmines/DBFilesClient \
  --world-map-artwork        /home/smithkt/git/WDM-patch/minimal-deadmines/Interface/WorldMap/TheDeadmines
```

The suite covers descriptor layouts, the fixed-ID allocator (retention, collision, baseline
mismatch, retired leases), composer append-only/collision/determinism behavior, staging and read-back,
the parity artifact, the byte-exact Deadmines golden comparison, end-to-end EPF validation and
staging of all 24 tiles, and the rejection cases (schema 1, unknown keys, wrong types, zero and
duplicate IDs, missing keys, dangling floor references, artwork in the wrong directory, path
traversal, corrupt composed bytes).

It also pins the two compatibility rules this module depends on, so neither can regress unnoticed:

- **No-transform maps** — a map that omits `transform` parses, requests only the three always-present
  tables, has no `WorldMapTransforms` row, composes the stock transform bytes unchanged, and
  contributes no transform lease or hash. Covered for a single-floor map and for a multi-floor map,
  and the Deadmines golden run re-proves that adding a second, transform-less map changes the other
  three tables but leaves `WorldMapTransforms.dbc` at the 9 stock rows. A present transform must still
  be an object; duplicate transform IDs across maps are still rejected.
- **`dungeonMapId` as a reference** — `0`, `-1`, and a cross-map reference this package does not own
  are all accepted and composed into word 9 unchanged; a reference to a floor the same package owns
  under a different `mapId` is rejected. Out-of-range and non-integer values are rejected, as is a
  chunk that references another map's floor.

## Limitations

- Build 12340 only. There is no post-Cataclysm (`Map.dbc` / `MapArea`) support here.
- The composer owns exactly the four world-map tables; it rejects any other table.
- Baseline replacement is a deliberate `.content dbc review` / `.content dbc approve` action, never a
  silent hash override.
- These tests are client-side and require no MySQL. The database-backed harnesses in the rest of
  Phase 4 still require a local MySQL server, which this development machine does not have.
