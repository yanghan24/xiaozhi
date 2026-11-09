-- Identity preparation only. Request issuance and binding confirmation belong to #13.
CREATE TABLE customer_device_request (
    handle VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    device_id VARCHAR(32) NOT NULL,
    purpose VARCHAR(16) NOT NULL,
    permission_version BIGINT NOT NULL,
    expires_at_ms BIGINT NOT NULL,
    consumed BOOLEAN NOT NULL DEFAULT FALSE,
    CONSTRAINT fk_customer_request_device FOREIGN KEY (device_id) REFERENCES ai_device(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE customer_browser (
    id_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    csrf VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    last_activity_ms BIGINT NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
