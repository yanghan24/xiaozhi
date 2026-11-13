CREATE TABLE dc_ota_batch (
 batch_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 operator_id BIGINT NOT NULL,
 request_key CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NULL,
 firmware_id CHAR(32) NOT NULL,
 mode VARCHAR(16) NOT NULL,
 snapshot MEDIUMTEXT NOT NULL,
 status VARCHAR(32) NOT NULL,
 expires_at_ms BIGINT NOT NULL,
 created_at_ms BIGINT NOT NULL,
 UNIQUE KEY ota_batch_request(operator_id,request_key)
);
