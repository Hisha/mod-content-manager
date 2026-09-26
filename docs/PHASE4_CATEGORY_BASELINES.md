# Phase 4 extension: Hunts category and generic baseline provenance

This is the current installation and acceptance guide. It supersedes the per-DBC mandatory-pin instructions in the earlier Phase 2–4 documentation. The original native-currency acceptance on Eitrigg passed: Huntmaster's Seal appeared with quantity 20 and no ordinary bag-slot usage. This extension moves that same currency into an allocated native **Hunts** category. Its live Eitrigg acceptance remains pending.

## Native mechanism and bounded implementation

`CurrencyTypes.dbc.CategoryID` references `CurrencyCategory.dbc.ID` in `DBFilesClient/CurrencyCategory.dbc`. Build 12340 uses this WDBC record:

| Word | Field | Representation |
|---|---|---|
| 0 | ID | positive signed 32-bit category identity |
| 1 | Flags | signed 32-bit flags |
| 2–17 | Name | 16 unsigned 32-bit offsets into the DBC string block |
| 18 | NameFlags | unsigned 32-bit localized-string metadata |

That is **19 fields and 76 bytes per record**. There is no expansion or parent-category column in this build. The [WoWDBDefs CurrencyCategory definition](https://github.com/wowdev/WoWDBDefs/blob/master/definitions/CurrencyCategory.dbd) includes build 3.3.5.12340 in its ID/Flags/Name_lang layout. The local installed-client table has eight rows; category 22 names Dungeon and Raid. This corroborates the user's screenshot. Reference AzerothCore commit `06234df3d5ab26c93f4f1f06f3edb828b73ecd3c` does not load a server category store; its CurrencyTypes loader consumes the ItemID/BitIndex fields. Consequently this extension adds **no server category table**. It keeps the existing SQL overlay's CategoryID synchronized with the resolved client relationship.

The local category fingerprint is only research evidence. It is neither configured nor shipped as an accepted Eitrigg fingerprint. No baseline DBC or generated client MPQ is included in this overlay ZIP. Eitrigg supplies and registers its own validated baseline.

## Generic baseline registry

Administrators continue to select `ContentManager.BaselineDbcDirectory` and `ContentManager.ClientBuild`. Every semantic baseline used by a build is read through its compiled descriptor, validated, hashed in memory and from the file, and accepted through the same registry service. Supported tables are currently Item, CurrencyTypes and CurrencyCategory. Inspection uses the same validation path and is read-only; it does not require a pin or a prior registry entry.

The world database gains three Content Manager provenance tables:

- `content_manager_baseline`: current accepted identity, keyed by client build and exact table name, with descriptor version, SHA-256, revision, canonical source path, WDBC dimensions and acceptance time.
- `content_manager_baseline_history`: immutable acceptance revisions and actor/method metadata.
- `content_manager_baseline_review`: candidate identity/dimensions/source, the expected prior revision/hash/descriptor, review actor/time, and approval actor/time.

These are provenance metadata, not native currency category data.

On first build use, a validated unregistered table is automatically registered. Existing leases for its typed resource must agree with the inspected hash before automatic registration. A conflicting legacy lease causes the transaction to fail; Content Manager never guesses that a different file is a legitimate migration. Existing configured Item/CurrencyTypes SHA pins are also validated. First registration imports the matching established identity without modifying existing leases or old builds.

Subsequent builds require an exact hash **and descriptor-version** match. A changed file cannot silently replace the accepted identity. Approval and registration use a database lock, transactional guards and verified readback. Builds recheck source files before artifact assembly and guard the accepted registry identities again when committing the STAGED record.

`ContentManager.ItemBaselineSha256` and `ContentManager.CurrencyTypesBaselineSha256` remain optional stricter overrides. Existing active values may be left intact. Empty values enable normal registry-based operation. A nonempty invalid/mismatching pin still rejects builds and approvals. There is **no CurrencyCategoryBaselineSha256 requirement or setting**, and a future compiled DBC does not need its own SHA config entry.

A baseline replacement does not edit allocations or artifacts. A lease retains its original baseline fingerprint permanently. The allocator accepts that historical origin only when it appears in this table/build/descriptor's accepted history, then reruns current occupancy and policy checks. A newly accepted baseline claiming a retained item, known bit, or category ID still fails rather than renumbering it. Immutable parity manifests distinguish original lease provenance from the actual baseline snapshots used for that build. Existing format-1/2 artifacts remain readable; new registry-backed parity uses format 3. Old builds are verified against their recorded artifacts/leases, not reinterpreted using today's baseline files.

## Schema 2 declaration and allocation

The mod-hunts EPF is version **4.1.0**. Its readable `content/manifest.phase4.json` contains:

```json
"currencyCategories": [
  { "symbol": "hunts", "name": { "enUS": "Hunts" } }
],
"currencies": [
  { "symbol": "seal-currency", "item": "seal", "category": { "symbol": "hunts" } }
]
```

The unchanged logical Item and server item declarations remain in the manifest. A category reference is package-local. Every declared category must be used by a local currency. No concrete category ID, known bit, or item ID is authored. The legacy `categoryCopyFromItem` method remains supported, but a currency must select exactly one of that method or `category`.

`currency-category.id` is an independent persistent namespace, separate from `item.id` and `currency.known-bit`. Policy v1 allocates the lowest available positive value in 1–65535, sorting requests by logical identity. The upper bound is an operational index-size safeguard, not a claimed protocol limit or administrator-selected pool. Occupancy includes:

1. Every physical baseline CurrencyCategory ID.
2. Every nonzero CategoryID in baseline CurrencyTypes, including dangling references, even when outside the allocation range.
3. All retained category leases, including removed packages.
4. External SQL CurrencyTypes category references. Exactly owned, undrifted references backed by the same package's retained category lease may be reused.

Existing Seal leases remain **56807** (`mod-hunts/seal/item.id`) and **4** (`mod-hunts/seal-currency/currency.known-bit`). The resolved Hunts category value is discovered at acceptance; it is not an acceptance constant.

Composition preserves all baseline record values and string bytes, appends generated names deterministically, and sorts records by ID. New categories use Flags 0 and copy opaque NameFlags metadata from the lowest-ID baseline Flags-0 category having an enUS name. It refuses a baseline with no such template rather than inventing locale-flag semantics. Memory and disk readback validate the generated table. Parity records definitions, name maps, fallback policy, category leases, all composed DBC hashes, actual baseline snapshots, and the client/server category reference.

## Deterministic localization

Category names require authored enUS text. Supported authored locale keys are enUS, koKR, frFR, deDE, zhCN, zhTW, esES, esMX and ruRU, in that fixed slot order. For each supported slot, use its authored value when provided; otherwise write the exact enUS text. No translations are synthesized. For this package all nine supported slots therefore contain `Hunts`. Slots 9–15 are reserved in the compiled 12340 descriptor and remain offset zero (empty); authoring unknown/reserved locale keys is rejected. Thus absent supported translations explicitly fall back to enUS, while unsupported client locales outside this descriptor have no claimed display behavior. Live non-enUS display still needs client acceptance.

Names must be valid UTF-8, 1–255 bytes, with no NUL/control characters. String insertion order and interning are fixed; changing package discovery order does not change output bytes.

## Eitrigg installation and acceptance

### Installation preparation

Keep a backup of the current active config, module files/EPF, world database and current working client patch/sidecars. Overlay this ZIP's `mod-content-manager/` and `mod-hunts/` folders into the matching module repositories. The ZIP contains changed/new files only; do not replace either entire repository. No files in the supplied original repositories were edited during development.

Apply `mod-content-manager/data/sql/db-world/updates/2026_09_16_03_content_baseline_registry.sql` to **Eitrigg's world database**, using its normal authenticated administration connection. It only creates the three registry tables with `IF NOT EXISTS`; the updated base schema is for fresh installations. Existing Phase 4 tables and ownership must remain in place. Do not delete allocations, ownership rows or build records. Reconfigure the normal AzerothCore build to discover the new C++ files, compile/install, and restart worldserver. This development environment cannot supply or verify Eitrigg's build/service commands.

Keep the administrator-selected baseline directory and build 12340. Place the genuine pristine Eitrigg/client-baseline `CurrencyCategory.dbc` alongside Item.dbc and CurrencyTypes.dbc if it is not already present. Do not extract it from a Content Manager generated patch. If unavailable, the first inspection below will fail with a missing-file error; obtain the corresponding baseline before building. Preserve existing Item/CurrencyTypes pins if configured; do not add a category pin or copy any local research fingerprint. The ZIP changes `.conf.dist` only, never your active config.

### 1. First real-server acceptance command

Immediately after installation, the **first acceptance command must be**:

```text
.content dbc inspect CurrencyCategory
```

No configured category hash is needed. Expect build 12340, descriptor v1, 19 fields, 76 record bytes, valid strings/IDs, Eitrigg's computed SHA-256, and initially `UNREGISTERED`. Record the output. If registry SQL is missing, inspection can still report the file, but fix the reported registry error before building. If the file/layout validation fails, stop acceptance and correct the selected baseline; do not approve malformed data.

Then run:

```text
.content dbc inspect Item
.content dbc inspect CurrencyTypes
.content allocations
.content scan
```

Confirm the retained Seal ItemID **56807** and known bit **4**, and mod-hunts available version **4.1.0**. Neither inspect command registers or changes anything. Existing pins and leases must agree with the actual Item/CurrencyTypes baselines. No SHA calculation, copying or new configuration entry is required.

### 2. Select the upgraded EPF and build only

The existing version-selection workflow is explicit:

```text
.content uninstall mod-hunts
.content install mod-hunts
.content build
.content build list
.content allocations
```

Uninstall/install selects 4.1.0 and preserves deployed rows and retained leases. Keep the AQ package installed. Build should register all previously unregistered validated baselines, report all three hashes, reuse ItemID 56807 and bit 4, and report `mod-hunts/hunts/currency-category.id = <resolved value>`. Expect one added Item row, one added CurrencyTypes row, one added CurrencyCategory row, and the AQ content unchanged. The artifact and server states must be **STAGED**. Item_template and currencytypes_dbc must still have their pre-apply contents; the Seal's old CategoryID should still be 22.

Read-only SQL checks (select Eitrigg's actual world database first):

```sql
SELECT client_build,table_name,descriptor_version,sha256,revision,
       source_path,record_count,field_count,record_size,accepted_at
FROM content_manager_baseline ORDER BY client_build,table_name;

SELECT realm_name,package_key,symbol,resource_kind,allocated_value,baseline_sha256
FROM content_manager_allocation
WHERE realm_name='Eitrigg' AND package_key='mod-hunts'
ORDER BY resource_kind,symbol;

SELECT c.ID,c.ItemID,c.CategoryID,c.BitIndex,o.category_id,o.applied_build
FROM currencytypes_dbc c
JOIN content_manager_currency_owner o ON o.entry=c.ID
WHERE o.realm_name='Eitrigg' AND o.package_key='mod-hunts';
```

Do not require a particular category numeric value. It must be the retained `hunts` lease, free of all specified occupancy. Inspect the generated MPQ's `.server.json` and `.parity.json`: the server row is still ID=ItemID=56807 and BitIndex=4, but CategoryID equals that lease and categorySymbol is `hunts`; the category definition has enUS `Hunts`. The parity manifest must include the category DBC hash and all three baseline snapshots.

### 3. Activate (managed server apply runs first)

Replace `N` below with the newly reported build number:

```text
.content server status N
.content activate N
.content server status N
```

Activation must report **APPLIED** before client publication and update the existing owned currency row in place. The exact old relationship and owner identity must match before the transaction; an unowned or drifted row is rejected. No delete/recreate is used. The advanced `.content server apply N` command remains available for manual verification and recovery and uses the same apply path.

Verify resolved parity with read-only SQL:

```sql
SELECT i.entry,i.name,i.BagFamily,c.ID,c.ItemID,c.CategoryID,c.BitIndex,
       a.allocated_value AS hunts_category,o.category_id AS owned_category,
       (c.CategoryID=a.allocated_value AND c.CategoryID=o.category_id) AS category_parity
FROM content_manager_allocation a
JOIN content_manager_currency_owner o
  ON o.realm_name=a.realm_name AND o.package_key=a.package_key
JOIN currencytypes_dbc c ON c.ID=o.entry
JOIN item_template i ON i.entry=c.ItemID
WHERE a.realm_name='Eitrigg' AND a.package_key='mod-hunts'
  AND a.resource_kind='currency-category.id' AND a.symbol='hunts';
```

Expect entry=ID=ItemID=56807, BagFamily=8192, BitIndex=4, category_parity=1, and the same allocated category in all three columns. There is no server category-table install step.

Use the existing Portalkeeper/client patch process without changing Portalkeeper. Restart worldserver to reload its cached item/DBC SQL overlay, and restart the patched client. APPLIED describes the verified database state, not a running-cache reload.

### 4. Native client acceptance and deterministic rebuild

On the same acceptance character, open the native Currency tab. The existing Huntmaster's Seal quantity **20** must now appear under **Hunts**, without normal bag-slot usage. Stock categories and currencies should remain correct. No new Seal grant is needed if that stack remains. On a fresh test character only, after verifying ItemID 56807, `.additem 56807 20` can recreate the earlier quantity test; do not grant another 20 to the existing stack inadvertently.

Verify the AQ gong asset remains present. Hunt rewards, `hunt_stats.huntmaster_seals`, Huntmaster vendor behavior, HuntsUI and Portalkeeper must behave as before; this package changes no such code or balances. No ItemExtendedCost/vendor implementation is included.

Run `.content build` again. It must leave the new build STAGED, retain all three allocations and produce identical Item/CurrencyTypes/CurrencyCategory bytes for unchanged inputs. MPQ/sidecar build metadata may differ by build number; compare the composed DBC hashes in parity. Restart/rebuild must not choose a new category. Record a screenshot showing Hunts and quantity 20, the inspection outputs, resolved lease and parity/server status. Phase 4 extension acceptance is complete only after these live checks.

## Intentional future baseline replacement

Use a maintenance/test window. First preserve the currently accepted source file. Put the intended replacement in the configured baseline location, then:

```text
.content dbc inspect CurrencyCategory
.content dbc review CurrencyCategory
```

Inspect reports the hash mismatch without changing the accepted identity. Review validates the current file and records a numbered candidate, displaying old identity, new source/hash/descriptor/dimensions. Review/approval require administrator security or console access. Use the returned number `R` only after assessing that candidate:

```text
.content dbc approve CurrencyCategory R
```

Approval rereads/revalidates the source and requires the exact reviewed hash, descriptor and source path plus the expected old accepted revision/hash/descriptor. Stale reviews or intervening file/registry changes fail closed. The administrator does not calculate or type a SHA. The same commands work for Item and CurrencyTypes. For those two tables an existing explicit pin remains stricter: deliberately remove/update that optional override before approving different bytes, then reload config. Approval does not bypass a pin.

Run `.content build` after approval; it still performs all lease/collision checks and creates only STAGED artifacts. A replacement occupying a retained value will fail even after approval. Do not delete leases to force a build. Changing a compiled descriptor version is a separate code migration: historical leases from another descriptor are not automatically reinterpreted.

To reject an unapproved candidate, restore the previously accepted file; no registry edit is needed. To return from an approved replacement, restore the previous file and review/approve it as another audited revision. Approval alone does not roll back server or client content. For a deployed-content rollback, restore the backed-up world content/ownership/build state and matching client artifact together using the established maintenance procedure; never restore just a currency row without its ownership/provenance, and never renumber retained leases. This extension adds no automatic rollback command.

## Validation and limits

Local testing covers seven standalone suites, AzerothCore header syntax checks, actual production build/apply paths against an isolated MySQL database, and actual client DBC composition/MPQ extraction. Tests exercise unowned collisions, concurrent drift transaction rollback, UPDATE-only category migration, idempotence, generic registry migration, mismatched pins, changed baselines, explicit review/approval, preserved historical leases and deterministic output. See `tests/PHASE4_TESTS.md` and the ZIP's test-results file for reproduction and logs.

The local reference environment is not Eitrigg. No full Eitrigg worldserver build/link, real database worker-pool run, remote deployment, client installation, or new in-game Hunts-category acceptance was performed here. The previously reported native Currency-tab success is user-provided evidence for the old category-22 build, not proof of this extension's live category/localization behavior.
