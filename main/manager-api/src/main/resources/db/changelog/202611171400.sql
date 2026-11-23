-- Independent service catalogue. No resource keys or customer tokens are stored in roles.
CREATE TABLE speaker_mcp (
 id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 name VARCHAR(128) NOT NULL,
 url VARCHAR(2048) NOT NULL,
 transport VARCHAR(32) NOT NULL,
 headers_cipher TEXT NULL,
 enabled BOOLEAN NOT NULL DEFAULT FALSE,
 version BIGINT NOT NULL DEFAULT 1,
 updated_at_ms BIGINT NOT NULL
) ENGINE=InnoDB;

-- ai_agent IDs have environment-dependent collations; keep references as a separate directory.
CREATE TABLE speaker_agent_mcp (
 agent_id VARCHAR(64) NOT NULL,
 service_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 PRIMARY KEY(agent_id,service_id),
 FOREIGN KEY(service_id) REFERENCES speaker_mcp(id)
) ENGINE=InnoDB;

CREATE TABLE speaker_memory (
 scope CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 summary TEXT NOT NULL,
 updated_at_ms BIGINT NOT NULL
) ENGINE=InnoDB;

CREATE TABLE speaker_conversation (
 id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 device_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 customer_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 agent_id VARCHAR(64) NOT NULL,
 configuration_revision VARCHAR(255) NOT NULL,
 memory_scope CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 created_at_ms BIGINT NOT NULL,
 INDEX ix_speaker_conversation_owner(customer_id,device_id,created_at_ms)
) ENGINE=InnoDB;
