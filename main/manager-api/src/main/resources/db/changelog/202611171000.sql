CREATE TABLE speaker_grant (
 customer_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 client_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 access_cipher TEXT NOT NULL,
 refresh_cipher TEXT NULL,
 access_expires_at_ms BIGINT NOT NULL,
 token_status VARCHAR(24) NOT NULL DEFAULT 'active',
 token_version BIGINT NOT NULL DEFAULT 1,
 token_updated_at_ms BIGINT NOT NULL,
 credential_cipher TEXT NULL,
 credential_status VARCHAR(24) NOT NULL DEFAULT 'pending',
 credential_base VARCHAR(255) NULL,
 credential_expires_at_ms BIGINT NULL,
 generation CHAR(43) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 PRIMARY KEY(customer_id,client_id),
 FOREIGN KEY(customer_id) REFERENCES speaker_customer(id)
) ENGINE=InnoDB;

ALTER TABLE speaker_device_state ADD login_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL;
CREATE TABLE speaker_login (
 id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 credential_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 binding_version BIGINT NOT NULL,
 issuer VARCHAR(255) NOT NULL,
 client_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 status VARCHAR(24) NOT NULL,
 device_code_cipher TEXT NULL,
 tokens_cipher TEXT NULL,
 verification_uri VARCHAR(2048) NULL,
 user_code VARCHAR(64) NULL,
 interval_ms BIGINT NOT NULL DEFAULT 5000,
 next_poll_at_ms BIGINT NOT NULL,
 expires_at_ms BIGINT NOT NULL,
 updated_at_ms BIGINT NOT NULL,
 error_code VARCHAR(64) NULL,
 customer_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
 FOREIGN KEY(device_id) REFERENCES dc_device(device_id),
 FOREIGN KEY(customer_id) REFERENCES speaker_customer(id),
 INDEX ix_speaker_login_due(status,next_poll_at_ms)
) ENGINE=InnoDB;
