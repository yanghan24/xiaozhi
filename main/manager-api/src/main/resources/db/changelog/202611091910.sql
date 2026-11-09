CREATE TABLE customer_identity_flow (
    id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    browser_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    request_handle VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    purpose VARCHAR(16) NOT NULL,
    permission_version BIGINT NOT NULL,
    state_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
    nonce VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    verifier TEXT NOT NULL,
    expires_at_ms BIGINT NOT NULL,
    status VARCHAR(32) NOT NULL,
    CONSTRAINT fk_customer_flow_browser FOREIGN KEY (browser_hash) REFERENCES customer_browser(id_hash),
    CONSTRAINT fk_customer_flow_request FOREIGN KEY (request_handle) REFERENCES customer_device_request(handle)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
