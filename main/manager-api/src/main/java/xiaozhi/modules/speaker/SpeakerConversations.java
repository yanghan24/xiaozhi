package xiaozhi.modules.speaker;

import java.util.Map;
import java.util.UUID;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;

/** Ownership metadata for the original chat-history and memory services. */
@Service
public class SpeakerConversations {
    private final JdbcTemplate db;
    public SpeakerConversations(JdbcTemplate db) { this.db=db; }
    public String create(Map<String,Object> device,String revision,String scope) {
        String id=UUID.randomUUID().toString();
        db.update("INSERT INTO speaker_conversation(id,device_id,customer_id,agent_id,configuration_revision,memory_scope,created_at_ms) VALUES (?,?,?,?,?,?,?)",
                id,device.get("device_id"),device.get("customer_id"),revision.split(":",2)[0],revision,scope,System.currentTimeMillis());
        return id;
    }
    public Map<String,Object> find(String id) {
        var rows=db.queryForList("SELECT * FROM speaker_conversation WHERE id=?",id);
        return rows.isEmpty()?null:rows.getFirst();
    }
    public String memory(String scope) {
        var rows=db.queryForList("SELECT summary FROM speaker_memory WHERE scope=?",scope);
        return rows.isEmpty()?"":rows.getFirst().get("summary").toString();
    }
    public void saveMemory(String scope,String summary) {
        db.update("INSERT INTO speaker_memory(scope,summary,updated_at_ms) VALUES (?,?,?) ON DUPLICATE KEY UPDATE summary=VALUES(summary),updated_at_ms=VALUES(updated_at_ms)",
                scope,summary,System.currentTimeMillis());
    }
}
