CREATE TABLE customer_logout_receipt (
    jti_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    issuer VARCHAR(255) NOT NULL,
    sid VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NULL,
    subject VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NULL,
    issued_at_ms BIGINT NOT NULL,
    expires_at_ms BIGINT NOT NULL,
    received_at_ms BIGINT NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
