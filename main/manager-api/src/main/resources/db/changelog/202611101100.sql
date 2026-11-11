ALTER TABLE dc_device ADD local_enabled BOOLEAN NOT NULL DEFAULT FALSE;
ALTER TABLE dc_device DROP CHECK ck_dc_device_reason,
    ADD CONSTRAINT ck_dc_device_reason CHECK (reason IN ('unbound','user_signed_out','market_auth_required','market_auth_pending','device_disabled','ready'));
CREATE TABLE dc_voice_token (
    token_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    token_cipher TEXT NOT NULL,
    device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    credential_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    ownership_version BIGINT NOT NULL,
    permission_version BIGINT NOT NULL,
    expires_at_ms BIGINT NOT NULL,
    INDEX ix_voice_device (device_id,expires_at_ms),
    FOREIGN KEY(device_id) REFERENCES dc_device(device_id)
) ENGINE=InnoDB;
CREATE TABLE dc_voice_session (
    id_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    token_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    created_at_ms BIGINT NOT NULL,
    FOREIGN KEY(token_hash) REFERENCES dc_voice_token(token_hash)
) ENGINE=InnoDB;