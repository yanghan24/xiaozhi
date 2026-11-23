CREATE TABLE speaker_agent (
 id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 name VARCHAR(128) NOT NULL,
 prompt TEXT NOT NULL,
 llm_model VARCHAR(128) NOT NULL,
 asr_model VARCHAR(128) NOT NULL,
 tts_voice VARCHAR(128) NOT NULL,
 version BIGINT NOT NULL DEFAULT 1,
 created_at_ms BIGINT NOT NULL,
 updated_at_ms BIGINT NOT NULL
) ENGINE=InnoDB;
CREATE TABLE speaker_runtime (
 id INT PRIMARY KEY,
 default_agent_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
 revision BIGINT NOT NULL DEFAULT 1,
 FOREIGN KEY(default_agent_id) REFERENCES speaker_agent(id)
) ENGINE=InnoDB;
INSERT INTO speaker_runtime(id) VALUES (1);
CREATE TABLE speaker_voice_ticket (
 token_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 token_cipher TEXT NOT NULL,
 device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 credential_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 binding_version BIGINT NOT NULL,
 permission_version BIGINT NOT NULL,
 expires_at_ms BIGINT NOT NULL,
 INDEX ix_speaker_voice_ticket(device_id,expires_at_ms),
 FOREIGN KEY(device_id) REFERENCES dc_device(device_id)
) ENGINE=InnoDB;
CREATE TABLE speaker_voice_session (
 session_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 credential_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 binding_version BIGINT NOT NULL,
 permission_version BIGINT NOT NULL,
 expires_at_ms BIGINT NOT NULL,
 FOREIGN KEY(device_id) REFERENCES dc_device(device_id)
) ENGINE=InnoDB;
