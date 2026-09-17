# Phase 5 — ItemExtendedCost infrastructure

## Installation and first Eitrigg acceptance gate

This release changes **mod-content-manager only**. Keep the accepted mod-hunts 4.1.0 EPF and existing economy, Huntmaster store, HuntsUI, Portalkeeper and current active client patch unchanged. No consumer EPF, baseline DBC or generated MPQ is included.

1. Back up the module and the world database using the normal Eitrigg procedure. Apply the changed-files ZIP at the AzerothCore root: its files are rooted under `modules/mod-content-manager/`. Use the existing complete module as the starting point; this is not a full module distribution.
2. Apply `modules/mod-content-manager/data/sql/db-world/updates/2026_09_16_04_content_extended_cost.sql` to Eitrigg's **world** database through the existing module SQL deployment procedure. It creates only `content_manager_extended_cost_owner`; it does not alter core vendor tables, create costs, change retained allocations, or write character data. Do not run the base schema as a substitute for the update. The existing generic-baseline registry migrations must already be present.
3. Reconfigure/rebuild/install worldserver using Eitrigg's existing build procedure so CMake discovers the two new `.cpp` files, then restart it. Retain the administrator-controlled settings:

   ```ini
   ContentManager.ClientBuild = 12340
   ContentManager.BaselineDbcDirectory = "/home/smithkt/azerothcore-wotlk/env/data/dbc"
   ```

   Use the actual existing baseline directory if different. **Do not add an ItemExtendedCost SHA pin, copy a local research hash, or replace the baseline file with a fixture.** Existing optional Item/CurrencyTypes pins remain supported.
4. The **first runtime acceptance command** after installation is:

   ```text
   .content dbc inspect ItemExtendedCost
   ```

5. **Stop here.** Capture the complete output, including all ID/reference chunks, descriptor/build, source path, dimensions, hash, and registry status. Review the real Eitrigg output before creating, installing or building any semantic ItemExtendedCost content. Do not run a content build/apply/activate as part of this first acceptance. Inspection neither registers a baseline nor allocates resources nor writes server/client content.

Expected structural result: WDBC validation PASS, client build 12340, descriptor v1, 16 fields, 64-byte records; no string-offset fields. Record count, string-block size and SHA are **observations**, not locally prescribed values. `UNREGISTERED` is normal before first validated use. A registry mismatch or schema/reference-query failure must be investigated, never bypassed. The inspector needs read access to the real world overlay/vendor tables and character refund table; it requires no fingerprint configuration. A failed inspection is not acceptance.

The actual Eitrigg checkout/baseline is not available on this development machine. Local evidence and test fixtures are explicitly not Eitrigg acceptance. The accepted live allocations remain `mod-hunts/seal/item.id=56807`, `mod-hunts/seal-currency/currency.known-bit=4`, `mod-hunts/hunts/currency-category.id=5`; these numbers appear only as observed leases and test assertions, never semantic authoring.

## Exact build-12340 layout and evidence

WDBC header: magic plus four little-endian uint32 values (record count, field count, record size, string bytes). Each ItemExtendedCost record contains these 16 little-endian 32-bit words; there are no string offsets:

| Word | Byte | Field |
|---:|---:|---|
| 0 | 0 | ID |
| 1 | 4 | HonorPoints |
| 2 | 8 | ArenaPoints |
| 3 | 12 | ArenaBracket |
| 4–8 | 16–32 | ItemID_1 through ItemID_5 |
| 9–13 | 36–52 | ItemCount_1 through ItemCount_5 |
| 14 | 56 | RequiredArenaRating |
| 15 | 60 | ItemPurchaseGroup |

Descriptor v1 models the words as int32, matching the reference SQL representation and WoWDBDefs. Validated IDs are positive; negative words fail closed. Stock zero/unused fields and the entire string block are preserved. The local stock file contains three empty ItemID slots with nonzero counts: these are retained exactly, not normalized to authored-row rules. ItemPurchaseGroup is preserved even when nonzero; AzerothCore skips it and the bounded semantic API does not author it.

Evidence:

- AzerothCore reference checkout commit `06234df3d5ab26c93f4f1f06f3edb828b73ecd3c`: `src/server/shared/DataStores/DBCfmt.h`, format `niiiiiiiiiiiiiix`; `DBCStructure.h`, `ItemExtendedCostEntry`; and `data/sql/base/db_world/itemextendedcost_dbc.sql`, exactly the 16 ordered signed-int columns above. Some struct comments misnumber counts/rating; the actual arrays, format, SQL order and bytes agree on offsets 9–13 / 14 / 15.
- `src/server/game/DataStores/DBCStores.cpp` loads `ItemExtendedCost.dbc` with SQL overlay `itemextendedcost_dbc` and checks stock ID 2997 for the expected client era.
- [WoWDBDefs ItemExtendedCost definition](https://github.com/wowdev/WoWDBDefs/blob/master/definitions/ItemExtendedCost.dbd), build range ending 3.3.5.12340, corroborates the field order and 32-bit layout.
- Read-only StormLib extraction of the locally installed client's latest locale patch produced 972 records, 16 fields, 64 bytes per record and a one-byte string block; highest physical ID 2997. Local research SHA: `606622955569b472e9c9de13b352242f5f0ec8d8bda6c0c3667b9d07feffac8a`. **This is not an Eitrigg pin.** An older locale archive has 15 fields/60-byte records and is rejected for this descriptor.
- `Player.cpp` looks up the cost, checks/consumes Honor/Arena points and item requirements, and evaluates the minimum arena-team slot/rating. `ArenaBracket` 0, 1, 2 represents the minimum slot considered, not literal team sizes 2, 3, 5.
- `Item.cpp` refund save converts `GetPaidExtendedCost()` to uint16; `PlayerStorage.cpp` loads it as uint16; `item_refund_instance.paidExtendedCost` is unsigned smallint. This is a real end-to-end bound despite larger DBC/vendor columns.

## Identity and occupancy

`item-extended-cost.id` is an independent persistent namespace in the existing allocation registry, policy version 1. Generated IDs use **1..65535**, the intersection of positive DBC/SQL IDs and the native refund persistence width. Zero means no cost. There is no administrator-selected range or new configuration knob. Physical baseline IDs outside this generated domain are still preserved and treated as occupied.

The deterministic allocator selects the lowest available value after reserving:

- Every physical baseline ItemExtendedCost ID.
- Every world `itemextendedcost_dbc.ID`.
- Every nonzero `npc_vendor.ExtendedCost`, including rows belonging to vendor-reference templates and dangling references. Scanning all rows covers reference templates reached through negative vendor item values.
- Every nonzero `game_event_npc_vendor.ExtendedCost`.
- Every nonzero character `item_refund_instance.paidExtendedCost`.
- Every retained lease in this namespace, including removed/retired package identities.

A verified Content Manager-owned overlay row is excluded from *external* occupancy only after its exact 16-word snapshot, realm/package/symbol and retained lease agree. Its lease continues to reserve the ID. Missing, foreign or drifted ownership fails closed. Unknown/unavailable SQL schemas or occupancy queries fail closed. No vendor row is inserted, updated or deleted by this feature.

Baseline required ItemIDs, including dangling references, also reserve item identities before Item allocation. Overlay required ItemIDs enter world item occupancy; existing exact Content Manager item ownership permits legitimate reuse of that same item. Extended-cost requirements do not allocate an additional item identity.

## Minimal Schema 2 API

Illustrative future authoring only; **do not add this to live mod-hunts yet**:

```json
{
  "extendedCosts": [
    {
      "symbol": "seal-cost-5",
      "requirements": [
        { "item": { "symbol": "seal" }, "count": 5 }
      ]
    }
  ]
}
```

An explicit cross-package reference is supported:

```json
{ "item": { "package": "mod-hunts", "symbol": "seal" }, "count": 5 }
```

Omitting `package` means the declaring package. A local requirement must reference a declared client/server Item. A cross-package requirement must resolve to a client/server Item in the installed, version-matched packages selected for this cumulative build, with its existing `item.id` lease. A dormant lease by itself is insufficient. Cost-only packages are supported when their references resolve to those active items.

Each declaration has a unique logical `symbol` and 1–5 distinct item requirements. Numeric item IDs, numeric cost IDs, unknown properties, duplicate references, missing symbols, invalid quantities, and Schema 1 semantic declarations are rejected. Requirement slots are ordered by `(package,symbol)` deterministically. Unused slots and ItemPurchaseGroup are zero for newly authored rows.

Optional fields: `honorPoints`, `arenaPoints`, `arenaBracket`, `requiredArenaRating`, default zero. Points are nonnegative integers at most `INT32_MAX / 255 = 8421504`; counts are positive integers at most `UINT32_MAX / 255 = 16843009`. The reference core multiplies requirements by an unsigned-byte purchase quantity; point deduction converts that product to signed int32. These limits prevent overflow in that actual path. Rating is 0..INT32_MAX, bracket 0..2, and a nonzero bracket requires a nonzero rating. This phase deliberately requires item-backed costs: free/empty and honor-only semantic rows are outside the bounded API. Existing stock rows remain untouched.

## Baseline provenance and composition

The existing generic baseline registry handles ItemExtendedCost by compiled descriptor/table/build identity. Inspection and status are read-only. First validated build may register the current file automatically; any preexisting namespace lease must agree with that first accepted fingerprint. Later builds hash and validate the configured file again and require exact registry agreement. The registry's descriptor version, provenance metadata, immutable history, candidate-bound review and explicit approval apply unchanged. No new per-DBC SHA setting is required.

Intentional replacement uses the existing commands, **only after separate administrator review**:

```text
.content dbc inspect ItemExtendedCost
.content dbc review ItemExtendedCost
.content dbc approve ItemExtendedCost <review-id>
```

Approval does not bypass identity collisions: a replacement baseline occupying a retained cost ID still prevents builds. Lease-origin hashes remain immutable; parity captures the current accepted snapshot and apply verifies history for both snapshot and lease origin. Baselines are re-inspected before assembly and registry identities are guarded during commit.

All installed semantic declarations compose into one `DBFilesClient/ItemExtendedCost.dbc`. The composer preserves stock word arrays and strings, adds allocated rows, sorts records by ID, serializes, then reparses through descriptor v1. Disk readback checks bytes and all 16 authored words against the server representation. Raw ownership of that canonical path conflicts with semantic composition. With no extended-cost declarations, the feature does not load/register that baseline, allocate that namespace, require its occupancy schemas for existing builds, or add a DBC/artifact field. Existing Item/CurrencyTypes/Category composition and legacy artifact formats remain intact.

## Explicit server deployment and immutable artifacts

Build creates immutable **STAGED** MPQ/server/parity artifacts and persistent leases. It does not publish/activate a client patch or apply live item/cost rows. Format 4 server/parity artifacts carry logical and resolved cost references, quantities, requirement fields, cost namespace leases and the composed DBC hash. Formats 1–3 remain readable; builds without cost content preserve the existing format/canonical representation.

Explicit `.content server apply <build>` may insert the generated definitions into the core's existing `itemextendedcost_dbc` overlay with a `content_manager_extended_cost_owner` snapshot. It participates in the same guarded world transaction as item/currency apply. Unowned rows cannot be adopted. Preflight and transactional guards protect against collisions and drift; failures roll back the world transaction. Successful apply verifies exact definitions and provenance; worldserver restart is needed for SQL DBC loading. Client activation/publication remains a separate explicit operation. This infrastructure writes **no npc_vendor or event vendor rows** and does not create a Huntmaster consumer.

Once a cost definition has been applied, its 16 words are immutable under that logical lease. An unchanged definition may be reused with updated build provenance. Changing requirements/counts after apply is rejected; author a new cost symbol. This preserves the meaning of persisted refunds and historical builds. Removed costs/leases are retained, not recycled or deleted. There is no automatic client activation, uninstall, or economy rollback.

Concurrency boundary: Content Manager world transactions serialize through the existing build lock; definition/registry guards detect intervening changes. Character refund occupancy is read through CharacterDatabase, as existing item-instance occupancy is; it is not part of a distributed transaction. Coordinate external/manual database writers and baseline replacements during maintenance. Native new purchases cannot reference a not-yet-defined cost without an external writer introducing that reference. No new guarantee of cross-database atomicity is claimed.

## Tests and limitations

See `../tests/PHASE5_TESTS.md` for reproducible test prerequisites and coverage. The standalone suite includes real local DBC preservation and focused invalid-input/allocation/parser/parity tests. Disposable MySQL fixtures exercise the production Build and Apply entry points with bundled StormLib and real SQL; configuration/discovery and the DB connection adapter are test substitutes. They are not a full worldserver/client acceptance test.

No full Eitrigg worldserver build, production database operation, vendor purchase, refund runtime test or native client acceptance is claimed. The real baseline/schema/occupancy output is still unknown. Those are the remaining acceptance questions, and **the first live procedure ends at inspection**. The later consumer phase must separately validate real cost usage/refunds and decide any economy changes; this release makes none.

## Delivered files and local validation result

Local checks passed: all eight standalone suites; existing currency/category SQL Apply regression; existing cumulative Build/AQ/provenance regression; generic baseline registry SQL regression; the new four-package extended-cost Build/Apply fixture; and 13 core-facing translation-unit syntax checks. Deliberate transaction guard failures in the logs are expected rollback tests. Complete captured results are in `PHASE5_LOCAL_TEST_RESULTS.txt`. No Eitrigg runtime acceptance is claimed.

Changed/new module files:

- `README.md`
- `data/sql/db-world/base/content_manager_schema.sql`
- `data/sql/db-world/updates/2026_09_16_04_content_extended_cost.sql`
- `docs/PHASE5_ITEM_EXTENDED_COST.md`
- `docs/PHASE5_LOCAL_TEST_RESULTS.txt`
- `src/ContentAllocationRegistry.cpp`
- `src/ContentAllocationRegistry.h`
- `src/ContentBaselineRegistry.cpp`
- `src/ContentBuildService.cpp`
- `src/ContentCommands.cpp`
- `src/ContentExtendedCostServer.cpp`
- `src/ContentExtendedCostServer.h`
- `src/ContentPackage.cpp`
- `src/ContentPackage.h`
- `src/ContentResourceAllocator.h`
- `src/ContentServerBundle.cpp`
- `src/ContentServerBundle.h`
- `src/ContentServerDeployment.cpp`
- `src/DbcDescriptor.cpp`
- `src/ItemExtendedCostDbc.cpp`
- `src/ItemExtendedCostDbc.h`
- `tests/PHASE4_TESTS.md`
- `tests/PHASE5_TESTS.md`
- `tests/dbc_reader_tests.cpp`
- `tests/extended_cost_mysql_tests.cpp`
- `tests/extended_cost_tests.cpp`
- `tests/run_phase4.py`

The ZIP contains only these module files beneath `modules/mod-content-manager/`. The external file manifest records original and resulting SHA-256 values. No mod-hunts file, client baseline, generated MPQ, executable, or repository metadata is included.
