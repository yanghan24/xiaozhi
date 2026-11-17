CREATE TABLE customer_device_operation (
    id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    device_session_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    key_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    permission_version BIGINT NOT NULL,
    revision BIGINT NOT NULL,
    created_at_ms BIGINT NOT NULL,
    UNIQUE KEY uq_device_sign_out (device_session_id,key_hash),
    FOREIGN KEY (device_session_id) REFERENCES customer_device_session(id)
) ENGINE=InnoDB;