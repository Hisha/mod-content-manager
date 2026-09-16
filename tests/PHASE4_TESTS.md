# Phase 4 tests

## Standalone tests

Requires Python 3 and a C++17 compiler. From the module directory:

```sh
python3 tests/run_phase4.py --hunts-epf ../mod-hunts/content/mod-hunts.epf
```

Optionally add `--currency-dbc /path/to/verified/CurrencyTypes.dbc` for read-only composition against real baseline bytes. Tests never install generated DBCs. The schema validation test's temporary EPF is under the OS temporary directory; do not run copies of that legacy test concurrently.

Suites cover WDBC parsing, allocation, schema-1 raw EPFs, schema-2 authoring, signed vendor references, server bundle parsing, explicit SQL collations, ownership classification, CurrencyTypes composition, bit bounds/exhaustion/retention, deterministic multi-row composition and parity mismatches.

## Disposable MySQL integration harnesses

`mysql_adapter/` is a test-only synchronous adapter around libmysqlclient. It runs the module's production SQL against real MySQL. It is **not** a substitute for worldserver acceptance: it does not exercise the real database worker pool, Field metadata diagnostics, startup loading or the client.

The C++ programs explicitly connect as root with no password over the supplied Unix socket, to the database `phase4_test`. Use only a newly initialized private MySQL instance with TCP disabled. Never point these harnesses at an existing server. Both require empty fixture tables and an explicit artifact directory.

Prepare `phase4_test` with utf8mb4_unicode_ci and load the module's `data/sql/db-world/base/content_manager_schema.sql`, plus CREATE TABLE definitions (without stock data) for item_template and currencytypes_dbc from the reference core. Test connections deliberately use utf8mb4_0900_ai_ci. No DROP/CREATE of a production database is part of a shipped test runner.

For `currency_mysql_tests.cpp`, compile it with the `tests/mysql_adapter` include directory BEFORE `src`, mysqlclient headers/libs and OpenSSL, and these production sources:

```text
ContentServerDeployment.cpp ContentServerBundle.cpp ContentCurrencyServer.cpp
ContentServerOwnership.cpp ContentAllocationRegistry.cpp ContentBuildRegistry.cpp
ContentBuildHash.cpp ContentBuildPublisher.cpp ServerTableDescriptor.cpp
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

Create a fresh fixture root containing `baseline/Item.dbc` and `baseline/CurrencyTypes.dbc` read-only copies. This fixture expects the handoff's 46,096-row Item baseline and the locally inspected 26-row CurrencyTypes file. Compile using the test adapter, module sources above except ContentServerDeployment, and:

```text
ContentBuildService.cpp ContentPackage.cpp ContentPackageRegistry.cpp
ContentResourceAllocator.cpp CurrencyDbcComposer.cpp ItemDbcComposer.cpp
DbcReader.cpp DbcDescriptor.cpp MpqBuilder.cpp third_party/miniz/miniz.c
```

Link the **bundled version** of StormLib, mysqlclient and OpenSSL. The fixture injects ContentManager config/discovery methods, so do not link ContentManager.cpp into it. Run with:

```text
PRIVATE_SOCKET FIXTURE_ROOT /path/to/mod-hunts.epf /path/to/aq-scarab-gong-marker.epf
```

It runs the production cumulative Build entry point twice, checks no live content writes, retained ItemID 56807, automatic bit assignment, stable registry leases, identical DBC bytes on rebuild, exact AQ raw-asset bytes inside the MPQ, and both composed DBCs. Fixtures and MPQs are test artifacts, not deployment files.
