package xiaozhi.modules.speaker;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.core.namedparam.NamedParameterJdbcTemplate;
import org.springframework.stereotype.Service;
import xiaozhi.modules.agent.dto.AgentChatSessionDTO.SpeakerOwner;

/** Ownership metadata for the original chat-history and memory services. */
@Service
public class SpeakerConversations {
    private final JdbcTemplate db;
    private final NamedParameterJdbcTemplate namedDb;

    public SpeakerConversations(JdbcTemplate db) {
        this.db = db;
        this.namedDb = new NamedParameterJdbcTemplate(db);
    }
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

    public Map<String, SpeakerOwner> findOwners(String agentId, List<String> sessionIds) {
        if (sessionIds.isEmpty()) {
            return Map.of();
        }
        return namedDb.query("""
                SELECT h.id, h.device_id, c.display_name, c.subject, d.name AS device_name
                FROM speaker_conversation h
                LEFT JOIN speaker_customer c ON c.id = h.customer_id
                LEFT JOIN speaker_device_state d ON d.device_id = h.device_id
                WHERE h.agent_id = :agentId AND h.id IN (:sessionIds)
                """, Map.of("agentId", agentId, "sessionIds", sessionIds), rows -> {
                    Map<String, SpeakerOwner> owners = new HashMap<>();
                    while (rows.next()) {
                        owners.put(rows.getString("id"), new SpeakerOwner(rows.getString("display_name"),
                                rows.getString("subject"), rows.getString("device_name"), rows.getString("device_id")));
                    }
                    return owners;
                });
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
