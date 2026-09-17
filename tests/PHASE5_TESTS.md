# Phase 5 test handoff

These are local development tests, not Eitrigg runtime acceptance. The live procedure in `../docs/PHASE5_ITEM_EXTENDED_COST.md` stops at `.content dbc inspect ItemExtendedCost`.

## Standalone suites

From the module directory, with Python 3 and a C++17 compiler:

```sh
python3 tests/run_phase4.py \
  --hunts-epf ../mod-hunts/content/mod-hunts.epf \
  --currency-dbc /path/to/read-only/CurrencyTypes.dbc \
  --extended-cost-dbc /path/to/read-only/ItemExtendedCost.dbc
```

The runner retains its name for compatibility and now runs eight suites: dbc_reader, phase2, occupancy, server_bundle, schema2, currency, category, extended_cost. The DBC arguments are optional and read-only. Do not run copies concurrently: legacy suites share temporary fixture names. No database or installed client file is written. Existing unsupported-table coverage now uses `Spell`, because `ItemExtendedCost` is supported.

`extended_cost_tests.cpp` checks the exact 16-word descriptor, malformed dimensions/truncation/IDs, exact stock words/string preservation including empty-item/nonzero-count quirks, deterministic multi-package composition, complete readback, strict counts/overflow limits, arena fields, symbolic parser rules, unknown/concrete fields, local/cross-package references, changed-manifest rejection, canonical artifact parsing, independent deterministic allocation, retired occupancy, restart-style retained lease reuse and approved-history behavior. With the optional real local file, it compares every baseline record against the generated document.

## Private MySQL integration

Use the existing test adapter and private-MySQL procedure in `PHASE4_TESTS.md`. **Never run these harnesses against an existing server.** They connect to the fixed test database `phase4_test` as root over the supplied private socket. The instance used during development had networking disabled. Harness setup/cleanup is intentionally not an automatic production SQL runner.

Start with a fresh disposable database using utf8mb4_unicode_ci, then import the module base schema and the reference core's CREATE TABLE definitions (no stock data) for:

```text
item_template
currencytypes_dbc
itemextendedcost_dbc
```

Add the following **test-only** tables to that disposable database; both adapter pools connect to it so the character tables are colocated only in the fixture:

```sql
CREATE TABLE npc_vendor(item int, ExtendedCost int unsigned NOT NULL DEFAULT 0) ENGINE=InnoDB;
INSERT INTO npc_vendor(item) VALUES(-56808),(0);
CREATE TABLE game_event_npc_vendor(ExtendedCost int unsigned NOT NULL DEFAULT 0) ENGINE=InnoDB;
CREATE TABLE item_refund_instance(paidExtendedCost smallint unsigned NOT NULL DEFAULT 0) ENGINE=InnoDB;
CREATE TABLE playercreateinfo_item(itemid int unsigned);
CREATE TABLE creature_equip_template(ItemID1 int unsigned, ItemID2 int unsigned, ItemID3 int unsigned);
CREATE TABLE item_instance(itemEntry int unsigned);
```

Create a private fixture root with **disposable copies** of Item.dbc, CurrencyTypes.dbc, CurrencyCategory.dbc and ItemExtendedCost.dbc in `baseline/`. This integration test deliberately modifies those copies for drift/replacement tests; never use a real baseline directory. The fixture assumes the local Wrath baselines used for Phase 4, with retained Seal ID/bit/category values available; those are test data, not authoring constants.

Compile `extended_cost_mysql_tests.cpp` with `tests/mysql_adapter` before `src` on the include path, libmysqlclient headers, bundled StormLib headers, C++17, and these production units:

```text
ContentServerDeployment ContentBuildService ContentPackage ContentPackageRegistry
ContentResourceAllocator ItemDbcComposer CurrencyDbcComposer CurrencyCategoryDbcComposer
ItemExtendedCostDbc ContentExtendedCostServer DbcReader DbcDescriptor MpqBuilder
ContentServerBundle ContentCurrencyServer ContentServerOwnership ContentAllocationRegistry
ContentBuildRegistry ContentBuildHash ContentBuildPublisher ServerTableDescriptor ContentBaselineRegistry
```

Compile each unit's `.cpp` plus `src/third_party/miniz/miniz.c`; link the module's bundled StormLib static library, mysqlclient and OpenSSL crypto. Do not link ContentManager.cpp: the fixture supplies config/discovery methods. Run:

```text
EXTENDED_COST_TEST PRIVATE_SOCKET PRIVATE_FIXTURE_ROOT /path/to/mod-hunts.epf /path/to/aq-scarab-gong-marker.epf
```

The fixture creates only temporary cost-test EPFs referencing `mod-hunts/seal` logically. It never edits the supplied mod-hunts/AQ EPFs. It exercises production Build, registry, parser, MPQ, parity and Apply functions against real SQL. A deliberate duplicate-key transaction guard rejection is expected in rollback cases.

Coverage mapping to the requested acceptance tests:

| Requirement | Proof |
|---|---|
| 1–3 Format, malformed data, exact stock preservation | standalone descriptor/reader/composer against synthetic and actual local bytes |
| 4–5 Occupancy and server references | real MySQL overlay, vendor including negative-reference row, event vendor, refund and retired lease gaps |
| 6–8 Determinism, retention/restart, multiple packages | typed allocator tests and two cost-only EPFs; database connections reopened, leases reread, byte-identical rebuild |
| 9 Raw/semantic collision | cumulative Build refuses conflicting ItemExtendedCost raw package |
| 10–11 Symbolic existing-item resolution | both packages resolve the one retained Seal item; no extra item lease; unknown symbol fails |
| 12–14 Counts, generated readback, repeated bytes | counts 5/7, full descriptor reparse and deterministic DBC comparison |
| 15 Existing DBC output | no-cost build and cost build have identical Item/CurrencyTypes/Category bytes; existing cumulative regression also runs |
| 16 AQ coexistence | raw AQ MPQ entry compared byte-for-byte between cumulative builds |
| 17 Retained live identities | fixture seeds 56807/4/5 as database leases only and verifies unchanged values |
| 18–19 Provenance | read-only unregistered inspection; first build registration; registry history; unexpected drift refusal; approved replacement keeps lease origins; approved collision still refused |
| 20 Existing tests | seven prior standalone suites, existing currency/category SQL apply and cumulative build fixtures, generic baseline registry fixture |

Additional SQL checks: STAGED build leaves live templates/overlay/ownership untouched; unowned collision blocks apply; injected collision after preflight rolls back all world writes; apply/retry succeeds with exact ownership; vendor/event row counts unchanged; refund references to an applied cost remain legitimate; changed applied definition and owned-row drift fail; format-4 parity rejects changed resolved item IDs. The allocator commit also guards resolved cost identity conditions and accepted baseline snapshots inside its world transaction.

For the old MySQL fixtures, add `ItemExtendedCostDbc.cpp` and `ContentExtendedCostServer.cpp` to their link unit lists. Their existing no-cost fixture schemas remain sufficient: no ItemExtendedCost occupancy query is introduced when no cost content is selected.

The adapter does not exercise AzerothCore's async worker pool, Field strict metadata diagnostics, worldserver startup or native client purchases/refunds. Core-facing translation units were syntax-checked against reference commit `06234df3d5ab26c93f4f1f06f3edb828b73ecd3c`. No full core link or Eitrigg runtime test is claimed.
