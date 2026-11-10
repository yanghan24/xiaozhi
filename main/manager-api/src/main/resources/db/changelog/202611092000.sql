-- Successful ACK replay is independent of the latest stop-confirmation evidence.
CREATE TABLE dc_state_ack_history (
    device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    revision BIGINT NOT NULL,
    permission_version BIGINT NOT NULL,
    audio_stopped BOOLEAN NOT NULL,
    received_at_ms BIGINT NOT NULL,
    PRIMARY KEY (device_id, revision, permission_version, audio_stopped),
    FOREIGN KEY (device_id) REFERENCES dc_device(device_id)
) ENGINE=InnoDB;

-- Preserve the successful ACK available when upgrading the foundation schema.
INSERT INTO dc_state_ack_history (device_id,revision,permission_version,audio_stopped,received_at_ms)
SELECT device_id,revision,permission_version,audio_stopped,received_at_ms FROM dc_state_ack;
