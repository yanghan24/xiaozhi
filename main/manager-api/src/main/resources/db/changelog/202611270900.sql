ALTER TABLE speaker_grant
    ADD COLUMN credential_verified_at_ms BIGINT NOT NULL DEFAULT 0,
    ADD COLUMN credential_check_after_ms BIGINT NOT NULL DEFAULT 0;
