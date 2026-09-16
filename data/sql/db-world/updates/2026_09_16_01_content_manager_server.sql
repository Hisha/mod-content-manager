-- Phase 3 server/client parity and explicit item_template deployment.
-- Rerunnable; preserves all existing builds, allocations, and item_template rows.
CREATE TABLE IF NOT EXISTS `content_manager_server_build` (
    `build_number` INT UNSIGNED NOT NULL,
    `bundle_filename` VARCHAR(255) NOT NULL,
    `bundle_sha256` CHAR(64) NOT NULL,
    `parity_filename` VARCHAR(255) NOT NULL,
    `parity_sha256` CHAR(64) NOT NULL,
    `server_state` VARCHAR(16) NOT NULL DEFAULT 'STAGED',
    `applied_at` TIMESTAMP NULL DEFAULT NULL,
    PRIMARY KEY (`build_number`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

CREATE TABLE IF NOT EXISTS `content_manager_item_owner` (
    `entry` INT UNSIGNED NOT NULL,
    `realm_name` VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    `package_key` VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    `symbol` VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    `resource_kind` VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    `applied_build` INT UNSIGNED NOT NULL,
    `artifact_sha256` CHAR(64) NOT NULL,
    `row_json` TEXT NOT NULL,
    PRIMARY KEY (`entry`),
    UNIQUE KEY `uq_item_owner_identity` (`realm_name`,`package_key`,`symbol`,`resource_kind`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;
