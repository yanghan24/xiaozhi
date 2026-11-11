CREATE TABLE dc_request (
    request_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    permission_version BIGINT NOT NULL,
    purpose VARCHAR(16) NOT NULL,
    verification_uri VARCHAR(512) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    created_at_ms BIGINT NOT NULL,
    expires_at_ms BIGINT NOT NULL,
    status VARCHAR(16) NOT NULL DEFAULT 'active',
    FOREIGN KEY (device_id) REFERENCES dc_device(device_id),
    INDEX (device_id),
    CHECK (purpose IN ('bind', 'resume', 'account')),
    CHECK (status IN ('active', 'expired', 'replaced', 'consumed'))
) ENGINE=InnoDB;

CREATE TABLE dc_request_operation (
    device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    key_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    intent VARCHAR(16) NOT NULL,
    expected_permission_version BIGINT NOT NULL,
    request_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    created_at_ms BIGINT NOT NULL,
    PRIMARY KEY (device_id, key_hash),
    FOREIGN KEY (device_id) REFERENCES dc_device(device_id),
    FOREIGN KEY (request_id) REFERENCES dc_request(request_id)
) ENGINE=InnoDB;
