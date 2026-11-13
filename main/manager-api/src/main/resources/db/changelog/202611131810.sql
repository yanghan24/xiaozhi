ALTER TABLE dc_device_presence
 ADD COLUMN report_at_ms BIGINT NULL,
 ADD COLUMN boot_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NULL,
 ADD COLUMN ota_capable BOOLEAN NOT NULL DEFAULT FALSE,
 ADD COLUMN idle BOOLEAN NOT NULL DEFAULT FALSE,
 ADD COLUMN system_info TEXT NULL;

CREATE TABLE dc_device_boot (
 device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 credential_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 boot_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 first_seen_at_ms BIGINT NOT NULL,
 PRIMARY KEY(device_id,credential_hash,boot_id),
 CONSTRAINT fk_dc_boot_device FOREIGN KEY(device_id) REFERENCES dc_device(device_id) ON DELETE CASCADE
) ENGINE=InnoDB;

ALTER TABLE dc_ota_operation
 ADD COLUMN ota_transport VARCHAR(16) NOT NULL DEFAULT 'voice',
 ADD COLUMN device_status VARCHAR(20) NULL;
