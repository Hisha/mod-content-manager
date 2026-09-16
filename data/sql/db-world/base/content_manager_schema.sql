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

-- A singleton InnoDB row serializes activation transactions across processes.
CREATE TABLE IF NOT EXISTS `content_manager_build_lock` (
    `id` TINYINT UNSIGNED NOT NULL,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB;
INSERT IGNORE INTO `content_manager_build_lock` (`id`) VALUES (1);

-- Retained logical Item IDs. No allocation is deleted on uninstall or build removal.
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
