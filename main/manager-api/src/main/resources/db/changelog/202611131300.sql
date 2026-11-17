CREATE TABLE dc_ota_preview (
 preview_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 operator_id BIGINT NOT NULL,
 device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 firmware_id CHAR(32) NOT NULL,
 snapshot TEXT NOT NULL,
 expires_at_ms BIGINT NOT NULL
);
