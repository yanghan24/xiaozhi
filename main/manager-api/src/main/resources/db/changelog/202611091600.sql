-- Product admission is separate from upstream ai_device / administrator ownership.
CREATE TABLE dc_device (
    device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    credential_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin UNIQUE,
    admitted BOOLEAN NOT NULL DEFAULT TRUE,
    owner_issuer VARCHAR(512) NULL,
    owner_subject VARCHAR(255) NULL,
    reason VARCHAR(32) NOT NULL DEFAULT 'unbound',
    revision BIGINT NOT NULL DEFAULT 1,
    permission_version BIGINT NOT NULL DEFAULT 1,
    current_request_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
    CHECK (revision BETWEEN 1 AND 9007199254740991),
    CHECK (permission_version BETWEEN 1 AND 9007199254740991),
    CHECK ((owner_issuer IS NULL) = (owner_subject IS NULL)),
    CHECK (reason IN ('unbound', 'user_signed_out', 'market_auth_required', 'device_disabled')),
    CHECK ((owner_subject IS NULL AND reason IN ('unbound', 'device_disabled')) OR
           (owner_subject IS NOT NULL AND reason <> 'unbound'))
) ENGINE=InnoDB;
