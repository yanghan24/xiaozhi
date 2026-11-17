-- Additive product foundation; hardware identity remains in dc_device.
CREATE TABLE speaker_customer (
 id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 issuer VARCHAR(255) NOT NULL,
 subject VARCHAR(255) NOT NULL,
 display_name VARCHAR(255) NULL,
 username VARCHAR(255) NULL,
 account_type VARCHAR(32) NULL,
 organization JSON NULL,
 profile_updated_at_ms BIGINT NULL,
 balance DECIMAL(30,8) NULL,
 balance_status VARCHAR(24) NOT NULL DEFAULT 'not_requested',
 balance_updated_at_ms BIGINT NULL,
 created_at_ms BIGINT NOT NULL,
 UNIQUE KEY uq_speaker_customer_identity (issuer,subject)
) ENGINE=InnoDB;

CREATE TABLE speaker_device_state (
 device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 name VARCHAR(128) NOT NULL,
 board_type VARCHAR(128) NULL,
 firmware_version VARCHAR(128) NULL,
 customer_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
 admin_enabled BOOLEAN NOT NULL DEFAULT TRUE,
 use_enabled BOOLEAN NOT NULL DEFAULT FALSE,
 binding_version BIGINT NOT NULL DEFAULT 1,
 permission_version BIGINT NOT NULL DEFAULT 1,
 last_seen_at_ms BIGINT NULL,
 bound_at_ms BIGINT NULL,
 created_at_ms BIGINT NOT NULL,
 CONSTRAINT fk_speaker_state_identity FOREIGN KEY(device_id) REFERENCES dc_device(device_id),
 CONSTRAINT fk_speaker_state_customer FOREIGN KEY(customer_id) REFERENCES speaker_customer(id),
 INDEX ix_speaker_customer_devices(customer_id)
) ENGINE=InnoDB;

CREATE TABLE speaker_event (
 id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 action VARCHAR(48) NOT NULL,
 actor VARCHAR(64) NOT NULL,
 created_at_ms BIGINT NOT NULL,
 INDEX ix_speaker_events(device_id,created_at_ms),
 FOREIGN KEY(device_id) REFERENCES dc_device(device_id)
) ENGINE=InnoDB;
