-- Identity requests can refer either to the compatibility fixture device or to
-- an independently admitted product request. Product admission never uses ai_device.user_id.
ALTER TABLE customer_device_request
    MODIFY device_id VARCHAR(32) NULL,
    ADD control_request_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL UNIQUE,
    ADD control_credential_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
    ADD CONSTRAINT fk_customer_control_request FOREIGN KEY (control_request_id) REFERENCES dc_request(request_id),
    ADD CONSTRAINT ck_customer_request_source CHECK ((device_id IS NULL) <> (control_request_id IS NULL));

-- Pre-integration QR handles have no identity-request association. Retire them
-- rather than silently adopting them with a new credential generation.
UPDATE dc_request SET status='replaced' WHERE status='active';
UPDATE dc_device SET current_request_id=NULL,revision=revision+1 WHERE current_request_id IS NOT NULL;
