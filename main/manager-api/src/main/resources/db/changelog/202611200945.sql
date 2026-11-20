CREATE TABLE dc_reminder (
 id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 ownership_version BIGINT NOT NULL,
 version BIGINT NOT NULL DEFAULT 1,
 due_at_ms BIGINT NOT NULL,
 title_cipher TEXT NOT NULL,
 status VARCHAR(16) NOT NULL,
 claimed_session CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
 ended_at_ms BIGINT NULL,
 INDEX ix_reminder_due(device_id,ownership_version,status,due_at_ms),
 INDEX ix_reminder_expiry(status,due_at_ms),
 INDEX ix_reminder_ended(ended_at_ms),
 FOREIGN KEY (device_id) REFERENCES dc_device(device_id) ON DELETE CASCADE
) ENGINE=InnoDB;
