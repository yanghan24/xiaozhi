ALTER TABLE speaker_device_state
    ADD COLUMN diagnostics_supported BOOLEAN NOT NULL DEFAULT FALSE,
    ADD COLUMN diagnostics_until_ms BIGINT NOT NULL DEFAULT 0;

CREATE TABLE speaker_device_diagnostic_log (
    device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    report_id CHAR(32) NOT NULL,
    firmware_version VARCHAR(64) NOT NULL,
    app_sha256 CHAR(64) NOT NULL,
    entries JSON NOT NULL,
    received_at_ms BIGINT NOT NULL,
    PRIMARY KEY (device_id, report_id),
    KEY idx_speaker_diagnostic_time (device_id, received_at_ms, report_id),
    CONSTRAINT fk_speaker_diagnostic_device FOREIGN KEY (device_id)
        REFERENCES speaker_device_state(device_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin;
