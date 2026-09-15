CREATE TABLE IF NOT EXISTS `content_manager_build` (
    `build_number` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `realm_name` VARCHAR(255) NOT NULL,
    `filename` VARCHAR(255) NOT NULL,
    `package_count` INT UNSIGNED NOT NULL,
    `file_count` INT UNSIGNED NOT NULL,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`build_number`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;
