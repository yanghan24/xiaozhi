ALTER TABLE dc_device ADD ownership_version BIGINT NOT NULL DEFAULT 0;
UPDATE dc_device SET ownership_version=1 WHERE owner_subject IS NOT NULL;

CREATE TABLE customer_device_session (
    id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    browser_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    flow_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
    device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    owner_subject_key CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    ownership_version BIGINT NOT NULL,
    permission_version BIGINT NOT NULL,
    revision BIGINT NOT NULL,
    created_at_ms BIGINT NOT NULL,
    FOREIGN KEY (browser_hash) REFERENCES customer_browser(id_hash),
    FOREIGN KEY (flow_id) REFERENCES customer_identity_flow(id),
    FOREIGN KEY (device_id) REFERENCES dc_device(device_id)
) ENGINE=InnoDB;

CREATE TABLE customer_binding_operation (
    browser_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    flow_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    key_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    device_session_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    created_at_ms BIGINT NOT NULL,
    PRIMARY KEY (browser_hash,flow_id,key_hash),
    FOREIGN KEY (device_session_id) REFERENCES customer_device_session(id)
) ENGINE=InnoDB;
