CREATE TABLE dc_device_presence (
 device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 credential_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 last_seen_at_ms BIGINT NOT NULL,
 CONSTRAINT fk_dc_presence_device FOREIGN KEY(device_id) REFERENCES dc_device(device_id) ON DELETE CASCADE
) ENGINE=InnoDB;
