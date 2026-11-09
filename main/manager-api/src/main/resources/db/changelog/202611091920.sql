ALTER TABLE customer_identity_flow ADD created_at_ms BIGINT NOT NULL DEFAULT 0,
    ADD subject_key CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
    ADD issuer VARCHAR(255) NULL,
    ADD subject VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NULL,
    ADD sid VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NULL,
    ADD identity_issued_at_ms BIGINT NULL;

CREATE TABLE customer_authorization (
    subject_key CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    generation VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    started_at_ms BIGINT NOT NULL,
    issuer VARCHAR(255) NOT NULL,
    subject VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    access_token TEXT NOT NULL,
    refresh_token TEXT NOT NULL,
    access_expires_at_ms BIGINT NOT NULL,
    business_key TEXT NOT NULL,
    key_expires_at_ms BIGINT NULL,
    status VARCHAR(32) NOT NULL,
    updated_at_ms BIGINT NOT NULL,
    INDEX ix_customer_refresh (status, access_expires_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
