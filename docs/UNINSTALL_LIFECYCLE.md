# Uninstall / remove lifecycle

`.content uninstall <package-key>` retires a package from the desired installed set
(the `content_manager_package` registry). This guide covers what uninstall does,
what it preserves, and the exact procedure for the common **INSTALLED - SOURCE
MISSING** case: a module whose EPF was removed or rolled back, which otherwise
dead-ends every `.content build`.

## What uninstall does

- Removes exactly one row: `DELETE FROM content_manager_package WHERE package_key = <key>`.
- Prints an **uninstall survey** first and a **post-removal report** afterwards.
- Never touches the remaining registry, and never rebuilds, activates, publishes,
  or applies server rows automatically.

Builds are cumulative: each build re-discovers the EPF of every currently installed
package and re-derives the MPQ from the resulting set. Removing a package therefore
removes its files from the *next* build without requiring its EPF bytes. This is why
a missing source is never a blocker for uninstall — it is only a blocker for builds
*while the package is still installed*.

## What uninstall preserves

Historical and ownership state is intentionally retained so rollback stays coherent:

| State | Behavior |
| --- | --- |
| Source EPF | Never deleted. If it is missing, uninstall neither needs nor recreates it. |
| `content_manager_build` | All builds, including ones containing the package, remain. |
| `content_manager_server_build` | Sidecar records remain. |
| Published artifacts | Retained. |
| `content_manager_allocation` | Leases remain (including retired ones; leases are never recycled). |
| Owner tables (`content_manager_item_owner`, `_currency_owner`, `_extended_cost_owner`, `_vendor_owner`) | Provenance rows remain. |
| Live server rows (`item_template`, `currencytypes_dbc`, `itemextendedcost_dbc`, `npc_vendor`) | Never auto-deleted. Ownership does not prove a row is unreferenced by live characters; remove separately with SQL only when they are provably unwanted. |
| Baseline / parity history, build numbering | Unchanged. |

Reinstalling the package later (`install`, then `build`) is deterministic: the same
logical leases and ownership persist, and the same content is re-derived.

## Output walk-through

```text
Uninstall survey:
  Package: mod-hunts                     <-| installed record
  State: INSTALLED - SOURCE MISSING      <-| source classification
  Installed version: 4.3.0
  Installed source: /path/.../mod-hunts.epf
  Currently discovered EPF: no           <-| discovery answer
  The original EPF is missing. Removal does not require or recreate it.
  Retained DBC allocations: 2 (builds 4..6); these leases are never recycled on removal
  Applied server content owned by this package:
    item_template: 1
    latest applied build: 6
  Build membership markers: builds 4..6
  The currently ACTIVE build (6) includes this package. Rebuild and activate to retire it.
  Removal changes only the desired installed package set: ...
```

Then, after a successful removal: the state transition (`INSTALLED - SOURCE MISSING
-> no current selection`), the remaining installed-package count, whether an EPF is
still discovered, and the required follow-up (`run .content build`, then
`.content activate <build-number>`, and `.content server apply <build-number>` only
if server rows should change).

## Not-installed answers

A package that is not installed is reported as one of three cases:

1. **AVAILABLE but not installed** — a valid discovered EPF declares the key. Use
   `.content install <package-key>` to select it.
2. **Not installed, but retained history remains** — leases or ownership exist; nothing
   was removed and the history is intact.
3. **Unknown to Content Manager** — nothing to uninstall.

## Procedure: remove a rolled-back module (SOURCE MISSING)

Primary target scenario: a solution module was rolled back so its EPF disappeared,
Content Manager still shows `INSTALLED - SOURCE MISSING`, and `.content build` fails
with `Package 'X': no discovered EPF source (missing or invalid)`.

1. Confirm the state:

   ```text
   .content scan
   ```

   The package shows `State: INSTALLED - SOURCE MISSING` and `No valid EPF with this
   key is currently discovered.`

2. Remove the stale selection:

   ```text
   .content uninstall <package-key>
   ```

   Review the survey. Leases and ownership rows remain listed but are never deleted.

3. Rebuild without the rolled-back package (no EPF needed):

   ```text
   .content build
   .content build list
   ```

4. Make the new build live when you are satisfied (activation is explicit):

   ```text
   .content activate <build-number>
   ```

5. If the old build had applied server rows, decide whether they should be retired.
   Uninstall does not delete them. They are documented in the owner tables:
   `content_manager_item_owner`, `content_manager_currency_owner`,
   `content_manager_extended_cost_owner`, `content_manager_vendor_owner` (each with
   `realm_name`, `package_key`, `applied_build`, and the `row_json` snapshot applied).
   Remove live rows only when you have confirmed nothing in-game references them.

### Example (per tested design intent)

A rollback removed `mod-hunts 4.3.0` (EPF gone) while `mod-native-social 1.0.0`
remained. `mod-hunts` was reported `INSTALLED - SOURCE MISSING`; builds were refused.
`uninstall mod-hunts` removed only its selection. `build` then produced a cumulative
MPQ from `mod-native-social` alone, recorded the build as STAGED, preserved every
historical build/sidecar and lease, and `activate` published it explicitly.

## Testing

- Standalone: `python3 tests/run_phase4.py` includes `package_lifecycle` (source
  classification, labels, retained-history, survey text; no database needed).
- Database integration: `tests/package_lifecycle_mysql_tests.cpp` uses two raw-only
  EPFs to exercise present-source removal, SOURCE MISSING removal, no-EPF rebuild,
  deterministic reinstall, and complete retention of build/sidecar/owner rows. See
  `tests/PHASE4_TESTS.md` for the disposable-MySQL procedure.