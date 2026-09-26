# Milestone 2 tests

`run_phase4.py` first performs a C++17 syntax-only compile of
`ContentManagedServer.cpp` and `ContentServerDeployment.cpp` against the compile mode of `tests/mysql_adapter`.
That mode exposes the same relevant `DatabaseWorkerPool::Query` overload shape
as AzerothCore, including the variadic template, without requiring MySQL client
headers or providing fake database behavior. This check covers the production
`Check`, `ApplySql`, and `Verify` template/SQL construction paths plus the
activation/deployment regression source even when the disposable-MySQL integration
test is unavailable.

`managed_server_tests.cpp` is part of `run_phase4.py` and covers the generic descriptor/parser, schema-2 compatibility, stable and non-recycled allocation, duplicate/collision rejection, canonical creature/gameobject/spawn representations, symbolic permanent-spawn resolution, parity, and symbolic vendor creature declarations. Existing schema 1–3 and resource suites remain in the same runner.

`managed_server_mysql_tests.cpp` follows the existing disposable-MySQL strategy. Compile it with `tests/mysql_adapter` before `src`, the MySQL client headers/library, C++17, and these production units:

- `ContentManagedServer.cpp`
- `ContentManagedServerDescriptor.cpp`
- `ContentServerBundle.cpp`
- `ContentVendorRow.cpp`
- `CurrencyCategoryDbcComposer.cpp`
- `CurrencyDbcComposer.cpp`
- `ItemExtendedCostDbc.cpp`
- `DbcReader.cpp`
- `DbcDescriptor.cpp`
- `ServerTableDescriptor.cpp`

Run it with the private disposable MySQL socket as its only argument. It recreates only fixture tables in the existing `phase4_test` database. Coverage includes existing/missing donor validation, generated creature and gameobject rows, package ownership snapshots, symbolic spawn resolution, post-apply verification, and rollback of all managed rows after an injected transaction failure.

The MySQL fixture is not a PTR or live-realm test. Do not point it at a realm database.

The disposable currency/deployment fixture also covers the activation workflow around
this implementation: client-only bypass, failed apply blocking publication and lifecycle
change, explicit apply, APPLIED verification without reapply, ACTIVE idempotence, and
STAGED apply-before-activate ordering.
