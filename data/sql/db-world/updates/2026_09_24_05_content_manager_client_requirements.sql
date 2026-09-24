-- Schema 3 client capability requirements for the mod-content-manager module.
-- Applied automatically by the AzerothCore database updater to an EXISTING
-- installation (this module's data/sql/db-world directory is an included update
-- path). Rerunnable/convergent for existing installs and fresh installs alike:
--   * CREATE TABLE IF NOT EXISTS only, no ALTER
--   * no DROP, no destructive operations
--   * no backfill, no INSERT
--   * never modifies existing content_manager_build rows
-- Existing builds naturally have zero requirement rows, which means "requires
-- nothing". The composite key is the duplicate guard. No foreign key is used so
-- historical build metadata stays conservative and independently readable.
CREATE TABLE IF NOT EXISTS `content_manager_build_client_requirement` (
    `build_number` INT UNSIGNED NOT NULL,
    `requirement` VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    PRIMARY KEY (`build_number`, `requirement`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;