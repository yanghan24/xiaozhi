CREATE TABLE speaker_enrollment (
    device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    proof_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    receipt_cipher TEXT NULL,
    FOREIGN KEY (device_id) REFERENCES speaker_device_state(device_id) ON DELETE CASCADE
) ENGINE=InnoDB;
