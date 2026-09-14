CREATE TABLE IF NOT EXISTS `content_manager_package` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `package_key` VARCHAR(128) NOT NULL,
    `name` VARCHAR(255) NOT NULL,
    `version` VARCHAR(64) NOT NULL,
    `source_file` VARCHAR(255) NOT NULL,
    `state` VARCHAR(32) NOT NULL DEFAULT 'DISCOVERED',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_content_manager_package_key` (`package_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `content_manager_id_registry` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `package_key` VARCHAR(128) NOT NULL,
    `content_type` VARCHAR(64) NOT NULL,
    `symbolic_key` VARCHAR(128) NOT NULL,
    `allocated_id` INT UNSIGNED NOT NULL,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_content_manager_symbol`
        (`package_key`, `content_type`, `symbolic_key`),
    UNIQUE KEY `uq_content_manager_allocated`
        (`content_type`, `allocated_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `content_manager_build` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `build_number` INT UNSIGNED NOT NULL,
    `state` VARCHAR(32) NOT NULL,
    `mpq_filename` VARCHAR(255) DEFAULT NULL,
    `sha256` CHAR(64) DEFAULT NULL,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_content_manager_build_number` (`build_number`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;