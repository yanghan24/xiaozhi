ALTER TABLE customer_identity_flow ADD authorization_generation VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL;
UPDATE customer_identity_flow f JOIN customer_authorization a
    ON f.subject_key=a.subject_key AND f.creation_order=a.started_order
    SET f.authorization_generation=a.generation;

ALTER TABLE dc_device DROP CHECK dc_device_chk_4,
    ADD CONSTRAINT ck_dc_device_reason CHECK (reason IN ('unbound','user_signed_out','market_auth_required','market_auth_pending','device_disabled'));
