# Phase 4 tests

The current suite includes the Hunts-category and generic-baseline extension. Live acceptance instructions are in `../docs/PHASE4_CATEGORY_BASELINES.md`.

## Standalone tests

Requires Python 3 and a C++17 compiler. From the module directory:

```sh
python3 tests/run_phase4.py --hunts-epf ../mod-hunts/content/mod-hunts.epf
```

Optionally add `--currency-dbc /path/to/verified/CurrencyTypes.dbc` for read-only composition against real baseline bytes. Tests never install generated DBCs. The schema validation test's temporary EPF is under the OS temporary directory; do not run copies of that legacy test concurrently.

Suites cover WDBC parsing, allocation, schema-1 raw EPFs, schema-2 authoring, signed vendor references, server bundle parsing, explicit SQL collations, ownership classification, CurrencyTypes composition, bit bounds/exhaustion/retention, deterministic multi-row composition and parity mismatches.

## Disposable MySQL integration harnesses

`mysql_adapter/` is a test-only synchronous adapter around libmysqlclient. It runs the module's production SQL against real MySQL. It is **not** a substitute for worldserver acceptance: it does not exercise the real database worker pool, Field metadata diagnostics, startup loading or the client.

The C++ programs explicitly connect as root with no password over the supplied Unix socket, to the database `phase4_test`. Use only a newly initialized private MySQL instance with TCP disabled. Never point these harnesses at an existing server. All harnesses require fresh empty fixture tables and an explicit artifact directory.

Prepare `phase4_test` with utf8mb4_unicode_ci and load the module's `data/sql/db-world/base/content_manager_schema.sql`, plus CREATE TABLE definitions (without stock data) for item_template and currencytypes_dbc from the reference core. Test connections deliberately use utf8mb4_0900_ai_ci. No DROP/CREATE of a production database is part of a shipped test runner.

For `currency_mysql_tests.cpp`, compile it with the `tests/mysql_adapter` include directory BEFORE `src`, mysqlclient headers/libs and OpenSSL, and these production sources:

```text
ContentServerDeployment.cpp ContentServerBundle.cpp ContentCurrencyServer.cpp
ContentServerOwnership.cpp ContentAllocationRegistry.cpp ContentBuildRegistry.cpp
ContentBuildHash.cpp ContentBuildPublisher.cpp ServerTableDescriptor.cpp
CurrencyCategoryDbcComposer.cpp CurrencyDbcComposer.cpp DbcReader.cpp DbcDescriptor.cpp
ContentBaselineRegistry.cpp
```

Run the resulting executable with `PRIVATE_SOCKET ARTIFACT_DIRECTORY`. It tests the actual Apply entry point, Phase 3 upgrade, unowned collision, an injected post-preflight drift rollback, ownership drift, idempotence and a second build apply. A deliberate duplicate-key error from the transaction guard is expected during the rollback test.

For `currency_build_mysql_tests.cpp`, start with a fresh fixture database and also create empty occupancy tables:

```sql
CREATE TABLE npc_vendor(item int);
INSERT INTO npc_vendor VALUES(-56808),(0);
CREATE TABLE playercreateinfo_item(itemid int unsigned);
CREATE TABLE creature_equip_template(ItemID1 int unsigned,ItemID2 int unsigned,ItemID3 int unsigned);
CREATE TABLE item_instance(itemEntry int unsigned);
```

Create a fresh fixture root containing `baseline/Item.dbc`, `baseline/CurrencyTypes.dbc`, and `baseline/CurrencyCategory.dbc` disposable copies. The extended test deliberately modifies those private copies to test baseline drift and approval; never point it at your real baseline directory. This fixture expects the handoff's 46,096-row Item baseline the locally inspected 26-row CurrencyTypes file, and the 8-row/19-field CurrencyCategory file. Compile using the test adapter, module sources above (including ContentServerDeployment for generated-artifact apply verification), and:

```text
ContentBuildService.cpp ContentPackage.cpp ContentPackageRegistry.cpp
ContentResourceAllocator.cpp CurrencyDbcComposer.cpp ItemDbcComposer.cpp
DbcReader.cpp DbcDescriptor.cpp MpqBuilder.cpp third_party/miniz/miniz.c
```

Link the **bundled version** of StormLib, mysqlclient and OpenSSL. The fixture injects ContentManager config/discovery methods, so do not link ContentManager.cpp into it. Run with:

```text
PRIVATE_SOCKET FIXTURE_ROOT /path/to/mod-hunts.epf /path/to/aq-scarab-gong-marker.epf
```

It runs the production cumulative Build entry point with blank pins, seeds retained ItemID 56807 and known bit 4, and checks automatic baseline registration, all three DBCs, stable leases, identical DBC bytes on rebuild, exact AQ bytes, and no live content writes. It then approves a valid category baseline replacement, verifies immutable lease origin hashes and current snapshot parity, applies the real generated artifacts with acceptance-history checks, injects concurrent registry drift before commit, and approves a deliberately colliding baseline to prove collision refusal still holds. Fixtures and MPQs are test artifacts, not deployment files.

## Added category and provenance coverage

`category_tests.cpp` covers category string offset/UTF-8 validation, exact preservation of baseline records/strings, deterministic ordering, authored-locale/enUS fallback, empty reserved slots, physical/dangling-reference occupancy, independent/retired leases, approved baseline history and collision refusal. Schema 2 tests reject missing/duplicate category selection, concrete IDs, missing symbols and unsupported/empty names. Seven standalone suites run through `run_phase4.py`.

The SQL apply fixture now upgrades the owned category-22 Seal row to a category lease, with database triggers that forbid INSERT/DELETE on CurrencyTypes during that upgrade. It injects category drift between preflight and transaction, checks complete rollback and idempotence, and removes ownership to confirm the row cannot be adopted. Fixture category values are test data, never production authoring.

`baseline_mysql_tests.cpp` uses a fresh copy of the empty fixture schema, the same production sources as the SQL apply fixture, and `PRIVATE_SOCKET PRIVATE_FIXTURE_DIRECTORY` arguments. It writes a synthetic valid CurrencyCategory baseline into that disposable directory and checks read-only inspection without pins, automatic registration, legacy-lease agreement, stricter pin mismatch, immutable history/leases, changed-file/descriptor refusal, candidate-bound approval and stale review rejection. It does not run alongside the other MySQL harnesses because all use the same disposable database name.

Local core-facing syntax validation used AzerothCore commit `06234df3d5ab26c93f4f1f06f3edb828b73ecd3c`; no Eitrigg core checkout is available. No full worldserver link or actual client category display test is claimed. Production worker-pool transaction behavior and the real-server/native-client acceptance remain required.
