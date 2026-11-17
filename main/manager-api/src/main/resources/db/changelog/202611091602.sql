CREATE TABLE dc_state_ack (
    device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    revision BIGINT NOT NULL,
    permission_version BIGINT NOT NULL,
    audio_stopped BOOLEAN NOT NULL,
    received_at_ms BIGINT NOT NULL,
    FOREIGN KEY (device_id) REFERENCES dc_device(device_id)
) ENGINE=InnoDB;
