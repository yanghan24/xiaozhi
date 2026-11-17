-- Additive: existing one-hour tool grants retain their original expiry.
CREATE TABLE dc_delivery_credential (
    credential_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    token_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
    operator_id BIGINT NOT NULL,
    name VARCHAR(64) NOT NULL,
    created_at_ms BIGINT NOT NULL,
    revoked_at_ms BIGINT NULL,
    last_used_at_ms BIGINT NULL,
    INDEX idx_delivery_credential_operator (operator_id, created_at_ms)
) ENGINE=InnoDB;
