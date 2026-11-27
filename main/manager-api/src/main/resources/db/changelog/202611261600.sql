ALTER TABLE speaker_grant
    ADD COLUMN token_retry_count TINYINT UNSIGNED NOT NULL DEFAULT 0,
    ADD COLUMN token_retry_at_ms BIGINT NOT NULL DEFAULT 0,
    ADD COLUMN token_error VARCHAR(48) NULL;

ALTER TABLE speaker_device_state
    ADD COLUMN reauthorization_until_ms BIGINT NOT NULL DEFAULT 0;
