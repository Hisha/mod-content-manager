-- Current mod-content-manager world schema.
-- Rerunnable for this schema; preserves package selection and completed builds.
-- CREATE IF NOT EXISTS intentionally does not upgrade incompatible development tables.

CREATE TABLE IF NOT EXISTS `content_manager_package` (
    `package_key` VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    `name` VARCHAR(255) NOT NULL,
    `version` VARCHAR(64) NOT NULL,
    `provider` VARCHAR(255) NOT NULL,
    `source_path` TEXT NOT NULL,
    `installed_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`package_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

-- Phase 3 sidecars are immutable; deployment state is independent of client activation.
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

-- A row is owned only after the explicit server apply transaction inserts this
-- provenance. A retained allocation alone is not ownership of item_template.
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

CREATE TABLE IF NOT EXISTS `content_manager_build` (
    `build_number` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `realm_name` VARCHAR(255) NOT NULL,
    `filename` VARCHAR(255) NOT NULL,
    `package_count` INT UNSIGNED NOT NULL,
    `file_count` INT UNSIGNED NOT NULL,
    `state` VARCHAR(16) NOT NULL DEFAULT 'STAGED',
    `sha256` CHAR(64) NOT NULL,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`build_number`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

-- Schema 3 client capability requirements, immutable per generated build. The
-- composite key is the duplicate guard; a build without rows requires nothing.
-- Existing builds naturally have no rows and need no backfill.
CREATE TABLE IF NOT EXISTS `content_manager_build_client_requirement` (
    `build_number` INT UNSIGNED NOT NULL,
    `requirement` VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    PRIMARY KEY (`build_number`, `requirement`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

-- A singleton InnoDB row serializes activation transactions across processes.
CREATE TABLE IF NOT EXISTS `content_manager_build_lock` (
    `id` TINYINT UNSIGNED NOT NULL,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB;
INSERT IGNORE INTO `content_manager_build_lock` (`id`) VALUES (1);

-- Retained logical resource IDs. No allocation is deleted on uninstall or build removal.
CREATE TABLE IF NOT EXISTS `content_manager_allocation` (
    `realm_name` VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    `package_key` VARCHAR(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    `symbol` VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    `resource_kind` VARCHAR(32) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    `allocated_value` INT UNSIGNED NOT NULL,
    `state` VARCHAR(16) NOT NULL DEFAULT 'reserved',
    `first_build` INT UNSIGNED NOT NULL,
    `last_build` INT UNSIGNED NOT NULL,
    `baseline_sha256` CHAR(64) NOT NULL,
    `descriptor_version` INT UNSIGNED NOT NULL,
    `policy_version` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`realm_name`, `package_key`, `symbol`, `resource_kind`),
    UNIQUE KEY `uq_allocation_value` (`realm_name`, `resource_kind`, `allocated_value`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;
-- Phase 4 ownership only. No authored currency ID or world content is installed here.
CREATE TABLE IF NOT EXISTS `content_manager_currency_owner` (
  `entry` int unsigned NOT NULL,
  `realm_name` varchar(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  `package_key` varchar(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  `item_symbol` varchar(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  `symbol` varchar(191) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  `category_id` int unsigned NOT NULL,
  `bit_index` int unsigned NOT NULL,
  `applied_build` int unsigned NOT NULL,
  `artifact_sha256` char(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  PRIMARY KEY (`entry`),
  UNIQUE KEY `currency_bit` (`bit_index`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

-- Generic baseline provenance. Does not alter existing builds, ownership, or allocation leases.
CREATE TABLE IF NOT EXISTS content_manager_baseline (
  client_build INT UNSIGNED NOT NULL,
  table_name VARCHAR(64) COLLATE utf8mb4_bin NOT NULL,
  descriptor_version INT UNSIGNED NOT NULL,
  sha256 CHAR(64) COLLATE utf8mb4_bin NOT NULL,
  source_path TEXT NOT NULL,
  record_count INT UNSIGNED NOT NULL,
  field_count INT UNSIGNED NOT NULL,
  record_size INT UNSIGNED NOT NULL,
  string_bytes INT UNSIGNED NOT NULL,
  revision BIGINT UNSIGNED NOT NULL,
  accepted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (client_build,table_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

CREATE TABLE IF NOT EXISTS content_manager_baseline_history (
  client_build INT UNSIGNED NOT NULL,
  table_name VARCHAR(64) COLLATE utf8mb4_bin NOT NULL,
  descriptor_version INT UNSIGNED NOT NULL,
  sha256 CHAR(64) COLLATE utf8mb4_bin NOT NULL,
  source_path TEXT NOT NULL,
  record_count INT UNSIGNED NOT NULL,
  field_count INT UNSIGNED NOT NULL,
  record_size INT UNSIGNED NOT NULL,
  string_bytes INT UNSIGNED NOT NULL,
  revision BIGINT UNSIGNED NOT NULL,
  actor VARCHAR(255) NOT NULL,
  acceptance_method VARCHAR(64) NOT NULL,
  accepted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (client_build,table_name,revision)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

CREATE TABLE IF NOT EXISTS content_manager_baseline_review (
  client_build INT UNSIGNED NOT NULL,
  table_name VARCHAR(64) COLLATE utf8mb4_bin NOT NULL,
  descriptor_version INT UNSIGNED NOT NULL,
  sha256 CHAR(64) COLLATE utf8mb4_bin NOT NULL,
  source_path TEXT NOT NULL,
  record_count INT UNSIGNED NOT NULL,
  field_count INT UNSIGNED NOT NULL,
  record_size INT UNSIGNED NOT NULL,
  string_bytes INT UNSIGNED NOT NULL,
  review_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  request_token CHAR(36) COLLATE utf8mb4_bin NOT NULL,
  expected_revision BIGINT UNSIGNED NOT NULL,
  expected_sha256 CHAR(64) COLLATE utf8mb4_bin NOT NULL,
  expected_descriptor_version INT UNSIGNED NOT NULL,
  reviewed_by VARCHAR(255) NOT NULL,
  reviewed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  approved_by VARCHAR(255) NULL,
  approved_at TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (review_id),
  UNIQUE KEY (request_token)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

-- Ownership for explicitly applied extended-cost definitions. No vendor rows are created.
CREATE TABLE IF NOT EXISTS content_manager_extended_cost_owner (
  entry INT UNSIGNED NOT NULL,
  realm_name VARCHAR(255) COLLATE utf8mb4_bin NOT NULL,
  package_key VARCHAR(128) COLLATE utf8mb4_bin NOT NULL,
  symbol VARCHAR(128) COLLATE utf8mb4_bin NOT NULL,
  row_json TEXT COLLATE utf8mb4_bin NOT NULL,
  applied_build INT UNSIGNED NOT NULL,
  artifact_sha256 CHAR(64) COLLATE utf8mb4_bin NOT NULL,
  PRIMARY KEY (entry),
  UNIQUE KEY logical_owner (realm_name,package_key,symbol)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

-- One bounded owned vendor relationship per existing creature; no destructive migration.
CREATE TABLE IF NOT EXISTS `content_manager_vendor_owner` (
 `creature_entry` INT UNSIGNED NOT NULL,
 `realm_name` VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
 `package_key` VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
 `symbol` VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
 `original_flags` INT UNSIGNED NOT NULL,
 `row_json` LONGTEXT NOT NULL,
 `applied_build` INT UNSIGNED NOT NULL,
 `artifact_sha256` CHAR(64) NOT NULL,
 PRIMARY KEY (`creature_entry`), UNIQUE KEY `logical_vendor` (`realm_name`,`package_key`,`symbol`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;

-- Generic ownership/provenance for donor-derived server templates and permanent spawns.
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
