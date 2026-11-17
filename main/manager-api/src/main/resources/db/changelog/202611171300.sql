-- The original, complete agent configuration remains the source of truth.
-- Match the existing ID column's character set/collation across environments.
SET @speaker_agent_column = (SELECT CONCAT(COLUMN_TYPE,' CHARACTER SET ',CHARACTER_SET_NAME,' COLLATE ',COLLATION_NAME)
 FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='ai_agent' AND COLUMN_NAME='id');
SET @speaker_agent_alter = CONCAT('ALTER TABLE speaker_runtime ADD configured_agent_id ',@speaker_agent_column,
 ' NULL, ADD CONSTRAINT fk_speaker_configured_agent FOREIGN KEY(configured_agent_id) REFERENCES ai_agent(id)');
PREPARE speaker_agent_migration FROM @speaker_agent_alter;
EXECUTE speaker_agent_migration;
DEALLOCATE PREPARE speaker_agent_migration;
