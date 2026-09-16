# Phase 4: native Wrath currency infrastructure

This change adds a logical currency declaration, constrained known-bit allocation, cumulative CurrencyTypes composition, and explicit server deployment through AzerothCore's typed `currencytypes_dbc` overlay. Hunt rewards and purchases still use `hunt_stats.huntmaster_seals`. No mod-hunts gameplay code, UI, Portalkeeper code, or baseline DBC is changed.

## Evidence and environment boundary

Eitrigg itself was unavailable. Implementation was checked against the local AzerothCore checkout at commit `06234df3d5ab26c93f4f1f06f3edb828b73ecd3c`, and actual DBC bytes extracted read-only from the installed WoW client. These are reference checks, not a claim that Eitrigg has the same checkout or a completed in-game acceptance pass.

Reference source locations, relative to that AzerothCore checkout:

- `src/server/game/Entities/Item/ItemTemplate.h`: `BAG_FAMILY_MASK_CURRENCY_TOKENS = 0x2000`, `IsCurrencyToken()`.
- `src/server/game/Entities/Player/Player.h`: currency slots `[118,150)` (32 slots).
- `src/server/game/Entities/Player/PlayerStorage.cpp`: `CanStoreItem` routes tokens into hidden slots; storing there calls `AddKnownCurrency`. Existing ordinary-bag stacks are not migrated by this change. Hidden-slot exhaustion can fall back to ordinary inventory, so this is not an unlimited currency wallet.
- `src/server/game/Entities/Player/Player.cpp`: `AddKnownCurrency` looks up the item in `sCurrencyTypesStore` and sets the one-based bit in `PLAYER_FIELD_KNOWN_CURRENCIES`.
- `src/server/game/Entities/Object/Updates/UpdateFields.h`: known-currency field occupies two 32-bit words.
- `src/server/shared/DataStores/DBCStructure.h`, `DBCfmt.h`: CurrencyTypes format `xnxi`; ItemID is the server lookup key and BitIndex is loaded as uint32.
- `src/server/game/Globals/ObjectMgr.cpp`: item loading removes the currency bag-family bit if the item is absent from the server CurrencyTypes store.
- `src/server/game/DataStores/DBCStores.cpp`: baseline DBC load followed by `currencytypes_dbc` SQL overlay load.
- `src/server/shared/DataStores/DBCDatabaseLoader.cpp`: SQL loader orders by ID but indexes CurrencyTypes by ItemID.
- `data/sql/base/db_world/currencytypes_dbc.sql`: four signed `int NOT NULL` columns, ID primary key, InnoDB.
- Stock `item_template` entry 40752 (Emblem of Heroism) has BagFamily 8192; its CurrencyTypes row is `(101,40752,22,10)`.

The [WoWDBDefs CurrencyTypes definition](https://github.com/wowdev/WoWDBDefs/blob/master/definitions/CurrencyTypes.dbd) confirms the four signed 32-bit fields for builds 3.0.1.8622–3.3.5.12340. AzerothCore consumes the ItemID and BitIndex words as unsigned values; supported generated values are positive and fit the signed SQL representation. [AzerothCore's character documentation](https://www.azerothcore.org/wiki/characters) describes persistence of the known-currency mask.

ItemExtendedCost is used for vendor costs; it does not participate in these possession/known-currency code paths and is not implemented here. No character-schema addition or balance migration is necessary. Server currency data **is** necessary in addition to item_template and the client MPQ.

## Exact CurrencyTypes layout and observed baseline

WDBC header: magic, uint32 record count, uint32 field count, uint32 record size, uint32 string-block size. All words are little-endian.

| Column | Offset in record | Descriptor | Meaning |
|---|---:|---|---|
| ID | 0 | int32 | Client row identity; distinct from the item lookup key |
| ItemID | 4 | int32 | Item.dbc / item_template identity; AC lookup key |
| CategoryID | 8 | int32 | CurrencyCategory reference |
| BitIndex | 12 | int32 | One-based known-currency index; supported 1–64 |

No string-offset fields exist. The inspected stock file has a single NUL byte string block. Composition preserves its bytes and every baseline record value, then sorts all rows by ID.

Local client source: `Data/enUS/patch-enUS-2.MPQ`, `DBFilesClient/CurrencyTypes.dbc`:

- Magic `WDBC`; records **26**; fields **4**; record bytes **16**; string bytes **1**.
- SHA-256: `61ed134510b11f4c010e505d3167240924e4c105839c15c5ec473cbc51f1844d`.
- Occupied BitIndex values: **1, 2, 3, 5, 7–25, 27–29**.
- Local test allocation: **4**. This is an observation, never an authored value or Eitrigg promise.
- Eitrigg's actual baseline hash remains unverified. The shipped config pin is deliberately empty.

The local client's Item.dbc from `patch-enUS-3.MPQ` has 46,096 records and SHA-256 `d455bc30b59bc368b2a972a913864dd092d50695140b9513582100dc56ed777d`, matching the handoff. The test seeded the retained Seal allocation and confirmed that 56807 stayed unchanged.

## Schema 2 authoring

Keep the existing `dbcRows` Item declaration and `serverRows` item_template declaration for `seal`. Set its server `BagFamily` to **8192** and add this top-level array:

```json
"currencies": [
  {
    "symbol": "seal-currency",
    "item": "seal",
    "categoryCopyFromItem": 40752
  }
]
```

40752 is an existing stock **category donor**, not the identity of the new currency. This follows the existing `DisplayInfoID.copyFromItem` convention. The donor must occur in the pinned baseline; the resulting category is copied from it. The Hunt EPF retains its existing display donor, name, description, stack size 200, and other server fields. EPF version is **4.0.0**. `content/manifest.phase4.json` is the readable authoring copy of the packaged manifest.

Numeric currency/item identities and explicit bit values are rejected. Bit allocation is implicitly automatic. Symbols must be unique within the package, reference a package-local declared server Item, and have a one-to-one currency/item relationship. BagFamily 8192 without a currency declaration is rejected; unrelated BagFamily values remain unsupported.

## Allocation, composition and provenance

`item.id` and `currency.known-bit` use separate policies and leases in `content_manager_allocation`. Item allocation retains its existing safe dense-index policy. Currency policy version 1 searches 1–64, ascending, with requests sorted by logical identity. It excludes baseline bits, external SQL-overlay bits and all retained leases, including removed packages. Exhaustion, duplicate leases, changed baseline hashes, drift and collisions fail explicitly. No administrator range is required.

CurrencyTypes.ID is **derived from its resolved ItemID**. It is not independently authored or allocated. Both baseline ID and ItemID occupancy, plus unowned SQL ID/ItemID occupancy, are excluded during Item allocation. A retained identity conflicting with either is rejected rather than renumbered. This derivation preserves the reference SQL loader's ID/ItemID ordering; unsafe external overlay ordering is rejected. `currency.known-bit` remains independent of ItemID. Future independently allocated resources should have their own policies.

Each cumulative build creates exactly one Item.dbc and one CurrencyTypes.dbc for all installed semantic contributors. A raw-file owner of either composed target is rejected. Composition checks duplicate IDs, item references and bits; performs in-memory and disk readback; and leaves source baselines untouched. Parsed baseline bytes are hashed as well as the file, preventing a read/hash race from accepting inconsistent provenance.

The server bundle adds the resolved currency relationship beside its item row. Currency builds use bundle/parity format 2; existing Phase 3 format-1 bundles remain readable. Parity records both allocation identities, baseline fingerprints, policy/descriptor versions, both generated DBC hashes, MPQ/bundle hashes, and the CurrencyTypes ↔ currencytypes_dbc ↔ item_template relationship including BagFamily.

`.content build` records immutable STAGED artifacts and leases only. It never writes item_template, currencytypes_dbc or deployment ownership. Explicit `.content server apply` validates schema, provenance, allocations and collisions, then writes the SQL overlay and item_template together. `content_manager_currency_owner` tracks the applied relationship. Both new and retained rows require exact ownership checks; identical but unowned rows are still conflicts. SQL errors or failed transaction guards roll back the entire apply. Identity comparisons use explicit utf8mb4_bin; item text comparisons preserve explicit utf8mb4_unicode_ci. The overlay is loaded on the next administrator-controlled restart. No restart or client publication is automatic.

## Eitrigg installation and acceptance

1. Overlay the ZIP's `mod-content-manager/` and `mod-hunts/` changed files into their matching repositories. Reconfigure the normal AzerothCore build so the new C++ files are discovered, then compile/install by your usual process. Apply `data/sql/db-world/updates/2026_09_16_02_content_manager_currency.sql` through the module updater (or explicitly to the world DB). It creates ownership infrastructure only. Do not replace the stock currencytypes_dbc table or delete leases.
2. Keep your existing Item pin and baseline directory. Add `ContentManager.CurrencyTypesBaselineSha256 = ""` to your active module config. Restart worldserver to load the new module code. Run:

```text
.content dbc inspect CurrencyTypes
.content dbc inspect Item
.content allocations
```

Record the **real** server-reported CurrencyTypes hash, dimensions, layout and occupied bits. Confirm `mod-hunts / seal / item.id = 56807`. If dimensions or semantic checks fail, stop and inspect the deployed data; do not copy the local test hash blindly.

3. Set `ContentManager.CurrencyTypesBaselineSha256` to that verified hash, then reload configuration:

```text
.reload config
.content scan
.content uninstall mod-hunts
.content install mod-hunts
.content build
.content build list
.content allocations
```

Uninstall/reinstall is the existing package-version selection workflow; it preserves leases and deployed content. Leave the AQ package installed. Scan should show mod-hunts 4.0.0. Build must show the retained item identity, a separately allocated known bit, Item record count +1, CurrencyTypes count +1, parity, and STAGED state.

4. Let `N` denote the actual new build number. Inspect the generated `.server.json` and `.parity.json` beside its MPQ. Run these commands with N replaced by that number:

```text
.content server status N
.content server apply N
.content server status N
.content activate N
```

Server apply must report APPLIED. Activation is the existing explicit publication command. Verify the new patch through Portalkeeper and restart worldserver/client at an appropriate maintenance point. Worldserver restart is needed to load BOTH the SQL overlay and the item template. Status APPLIED means the database transaction is verified, not that the running DBC store has been reloaded.

5. Check the world DB without editing it:

```sql
SELECT package_key,symbol,resource_kind,allocated_value,baseline_sha256
FROM content_manager_allocation
WHERE package_key='mod-hunts'
ORDER BY resource_kind,symbol;

SELECT i.entry,i.name,i.BagFamily,c.ID,c.ItemID,c.CategoryID,c.BitIndex
FROM content_manager_allocation a
JOIN item_template i ON i.entry=a.allocated_value
JOIN currencytypes_dbc c ON c.ItemID=i.entry
WHERE a.package_key='mod-hunts' AND a.symbol='seal' AND a.resource_kind='item.id';
```

6. With the patched client and restarted server, on a test character without a leftover ordinary Seal stack, run:

```text
.additem 56807 20
```

Use 56807 only after confirming the retained allocation above. Verify the native Currency tab, quantity, known status and absence of normal-bag clutter. The module does not migrate old ordinary stacks. Confirm Hunt balance/rewards/store still use `hunt_stats.huntmaster_seals`, test a stock-client Hunt session, and verify the AQ gong asset remains present. Rebuild and restart; confirm both leases stay fixed. These live Eitrigg checks have **not** been run here.

## Phase 5 boundary

Add typed ItemExtendedCost composition, its independent ID policy and ownership/provenance, and a small native vendor spending proof. Keep reward/balance migration, stock-client bridging, native Hunt UI and store redesign in later phases.

## Validation results and regression status

- Six standalone suites passed: DBC reader, Phase 2 allocator/composer, signed item occupancy, Phase 3 server bundle/ownership/collation, Schema 2 validation, and new currency allocator/composer/parity tests.
- Real local CurrencyTypes bytes composed successfully, 26 → 27 rows. Bounds 0/65, duplicate IDs/items/bits, missing donors, retained collision/hash mismatch, exhaustion, use of bit 64, lease persistence and order-independent output were checked.
- All seven modified/new core-facing integration translation units passed syntax checks against the reference AzerothCore headers. The other changed production units compiled in standalone/integration harnesses.
- Bundled StormLib built successfully. The production cumulative Build entry point ran twice against isolated MySQL with the updated Hunt EPF and the real AQ Schema 1 EPF: two packages, three MPQ files, Item 46,096 → 46,097, CurrencyTypes 26 → 27, byte-for-byte preservation of the AQ asset, stable retained ItemID 56807 and known-bit lease, identical generated DBC bytes across builds, and zero live item_template/currencytypes_dbc rows written by build.
- The production server Apply entry point passed isolated MySQL 8.4 tests: upgrade of an owned Phase 3 item, unowned CurrencyTypes collision refusal, complete apply rollback after injected post-preflight item drift, idempotent apply, owned-currency drift refusal, occupied-owned-row exclusion during allocation, and a second build apply.
- SQL tests used connection `utf8mb4_0900_ai_ci`, database `utf8mb4_unicode_ci`, item text `utf8mb4_unicode_ci`, and Content Manager identity `utf8mb4_bin`. No collation error occurred. The deliberate duplicate-key guard error in the rollback test is expected.
- Phase 1 raw package staging/cumulative MPQ preservation, Phase 2 retained item allocation/composition, and Phase 3 parsing/parity/ownership/collation regression checks passed locally. The signed npc_vendor negative-reference/zero behavior remains intact.
- Both supplied source repositories remained unmodified; changes were made to workspace copies. No full worldserver link, real worker-pool test, live Eitrigg deployment, Portalkeeper install, in-game native Currency tab check, or live stock-client Hunt regression was run. Those acceptance steps remain required.

Reproduction details and harness limitations are in `tests/PHASE4_TESTS.md`.

## Exact changed/new files

- `mod-content-manager/README.md`
- `mod-content-manager/conf/mod_content_manager.conf.dist`
- `mod-content-manager/data/sql/db-world/base/content_manager_schema.sql`
- `mod-content-manager/data/sql/db-world/updates/2026_09_16_02_content_manager_currency.sql`
- `mod-content-manager/docs/PHASE4.md`
- `mod-content-manager/src/ContentAllocationRegistry.cpp`
- `mod-content-manager/src/ContentBuildHash.cpp`
- `mod-content-manager/src/ContentBuildHash.h`
- `mod-content-manager/src/ContentBuildService.cpp`
- `mod-content-manager/src/ContentCommands.cpp`
- `mod-content-manager/src/ContentCurrencyServer.cpp`
- `mod-content-manager/src/ContentCurrencyServer.h`
- `mod-content-manager/src/ContentManager.cpp`
- `mod-content-manager/src/ContentManager.h`
- `mod-content-manager/src/ContentPackage.cpp`
- `mod-content-manager/src/ContentPackage.h`
- `mod-content-manager/src/ContentResourceAllocator.cpp`
- `mod-content-manager/src/ContentResourceAllocator.h`
- `mod-content-manager/src/ContentServerBundle.cpp`
- `mod-content-manager/src/ContentServerBundle.h`
- `mod-content-manager/src/ContentServerDeployment.cpp`
- `mod-content-manager/src/ContentServerOwnership.cpp`
- `mod-content-manager/src/CurrencyDbcComposer.cpp`
- `mod-content-manager/src/CurrencyDbcComposer.h`
- `mod-content-manager/src/DbcDescriptor.cpp`
- `mod-content-manager/src/ServerTableDescriptor.cpp`
- `mod-content-manager/tests/PHASE4_TESTS.md`
- `mod-content-manager/tests/currency_build_mysql_tests.cpp`
- `mod-content-manager/tests/currency_mysql_tests.cpp`
- `mod-content-manager/tests/currency_tests.cpp`
- `mod-content-manager/tests/dbc_reader_tests.cpp`
- `mod-content-manager/tests/mysql_adapter/DatabaseEnv.h`
- `mod-content-manager/tests/mysql_adapter/Field.h`
- `mod-content-manager/tests/mysql_adapter/QueryResult.h`
- `mod-content-manager/tests/mysql_adapter/Transaction.h`
- `mod-content-manager/tests/run_phase4.py`
- `mod-content-manager/tests/schema2_tests.cpp`
- `mod-hunts/README.md`
- `mod-hunts/content/manifest.phase4.json`
- `mod-hunts/content/mod-hunts.epf`
