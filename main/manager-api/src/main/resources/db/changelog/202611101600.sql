-- Online delivery remains separate from customer ownership and voice permission.
CREATE TABLE dc_delivery_tool (
    token_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    operator_id BIGINT NOT NULL,
    created_at_ms BIGINT NOT NULL,
    expires_at_ms BIGINT NOT NULL
) ENGINE=InnoDB;

CREATE TABLE dc_delivery_operation (
    operation_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    operator_id BIGINT NOT NULL,
    device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
    hardware_id CHAR(12) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
    network_hardware_id CHAR(12) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
    board_type VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    credential_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    credential_cipher VARCHAR(2048) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    status VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT 'pending',
    created_at_ms BIGINT NOT NULL,
    device_verified_at_ms BIGINT NULL,
    write_operation_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NULL,
    completed_at_ms BIGINT NULL,
    FOREIGN KEY (device_id) REFERENCES dc_device(device_id),
    CHECK (hardware_id <> network_hardware_id),
    CHECK (status IN ('pending', 'delivered')),
    CHECK ((status = 'pending' AND completed_at_ms IS NULL AND write_operation_id IS NULL)
        OR (status = 'delivered' AND completed_at_ms IS NOT NULL AND write_operation_id IS NOT NULL
            AND device_verified_at_ms IS NOT NULL))
) ENGINE=InnoDB;
