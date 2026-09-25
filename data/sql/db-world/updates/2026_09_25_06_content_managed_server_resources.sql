-- Milestone 2: generic donor-derived server templates and permanent creature spawns.
-- This creates provenance only; it does not allocate IDs or alter AzerothCore content.
CREATE TABLE IF NOT EXISTS `content_manager_server_resource_owner` (
 `resource_kind` VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
 `entry` INT UNSIGNED NOT NULL,
 `realm_name` VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
 `package_key` VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
 `symbol` VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
 `row_json` LONGTEXT CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
 `applied_build` INT UNSIGNED NOT NULL,
 `artifact_sha256` CHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
 PRIMARY KEY (`resource_kind`,`entry`),
 UNIQUE KEY `logical_server_resource` (`realm_name`,`package_key`,`symbol`,`resource_kind`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;
