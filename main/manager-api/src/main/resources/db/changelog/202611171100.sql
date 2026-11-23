ALTER TABLE speaker_customer ADD balance_checked_at_ms BIGINT NULL;
CREATE TABLE speaker_device_operation (
 device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 request_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 credential_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 action VARCHAR(32) NOT NULL,
 permission_version BIGINT NOT NULL,
 created_at_ms BIGINT NOT NULL,
 PRIMARY KEY(device_id,request_hash),
 FOREIGN KEY(device_id) REFERENCES dc_device(device_id)
) ENGINE=InnoDB;
