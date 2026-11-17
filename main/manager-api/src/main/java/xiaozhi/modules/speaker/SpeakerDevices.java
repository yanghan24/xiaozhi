package xiaozhi.modules.speaker;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;
import static xiaozhi.modules.speaker.SpeakerSupport.*;

/** Device lifecycle authority. A heartbeat cannot supply customer identity or modify ownership. */
@Service
public class SpeakerDevices {
    private final JdbcTemplate db;
    private final SpeakerSettings settings;
    final TransactionTemplate transactions;

    public record Identity(String id, String hash) {
        @Override public String toString() { return "Identity[redacted]"; }
    }
    public SpeakerDevices(JdbcTemplate db, SpeakerSettings settings, PlatformTransactionManager manager) {
        this.db = db; this.settings = settings; this.transactions = new TransactionTemplate(manager);
        transactions.setIsolationLevel(org.springframework.transaction.TransactionDefinition.ISOLATION_READ_COMMITTED);
    }
    public Identity authenticate(String authorization, String id) {
        settings.requireEnabled();
        if (id == null || !id.matches("[A-Za-z0-9_.:-]{1,128}") || authorization == null
                || !authorization.matches("Bearer [A-Za-z0-9_-]{43,128}"))
            throw new SpeakerFailure(401, "INVALID_DEVICE_CREDENTIAL");
        Identity identity = new Identity(id, hash(authorization.substring(7)));
        load(identity, false);
        return identity;
    }
    public Map<String,Object> load(Identity identity, boolean lock) {
        var rows = db.queryForList("SELECT s.*,d.admitted,d.credential_hash FROM speaker_device_state s "
                + "JOIN dc_device d ON d.device_id=s.device_id WHERE s.device_id=? AND d.credential_hash=?"
                + (lock ? " FOR UPDATE" : ""), identity.id(), identity.hash());
        if (rows.size() != 1) throw new SpeakerFailure(401, "INVALID_DEVICE_CREDENTIAL");
        var row = rows.getFirst();
        if (!yes(row.get("admitted"))) throw new SpeakerFailure(403, "DEVICE_NOT_ADMITTED");
        return row;
    }
    public Map<String,Object> heartbeat(Identity identity, String board, String firmware) {
        return transactions.execute(tx -> {
            load(identity, true);
            long now = System.currentTimeMillis();
            db.update("UPDATE speaker_device_state SET last_seen_at_ms=?,board_type=COALESCE(?,board_type),"
                    + "firmware_version=COALESCE(?,firmware_version) WHERE device_id=?", now, board, firmware, identity.id());
            // One authenticated presence source is shared with hardware/OTA operations.
            db.update("INSERT INTO dc_device_presence(device_id,credential_hash,last_seen_at_ms) VALUES (?,?,?) "
                    + "ON DUPLICATE KEY UPDATE credential_hash=VALUES(credential_hash),last_seen_at_ms=VALUES(last_seen_at_ms)",
                    identity.id(), identity.hash(), now);
            return Map.of("device_id", identity.id(), "server_time_ms", now, "heartbeat_interval_ms", 20_000);
        });
    }
    public List<Map<String,Object>> list(String keyword, int page, int limit) {
        settings.requireEnabled();
        int size = Math.max(1, Math.min(100, limit));
        int offset = Math.max(0, Math.min(10000, page - 1)) * size;
        String search = "%" + (keyword == null ? "" : keyword.strip()) + "%";
        return db.queryForList("SELECT s.device_id,s.name,s.board_type,s.firmware_version,s.customer_id,"
                + "s.admin_enabled,s.use_enabled,s.permission_version,s.last_seen_at_ms,s.created_at_ms,c.display_name AS customer_name "
                + "FROM speaker_device_state s LEFT JOIN speaker_customer c ON c.id=s.customer_id "
                + "WHERE s.device_id LIKE ? OR s.name LIKE ? ORDER BY s.created_at_ms DESC,s.device_id LIMIT ? OFFSET ?",
                search, search, size, offset).stream().map(this::publicDevice).toList();
    }
    public long count(String keyword) {
        settings.requireEnabled();String search="%"+(keyword==null?"":keyword.strip())+"%";
        return db.queryForObject("SELECT COUNT(*) FROM speaker_device_state WHERE device_id LIKE ? OR name LIKE ?",Long.class,search,search);
    }
    public Map<String,Object> detail(String id) {
        settings.requireEnabled();
        var rows = db.queryForList("SELECT s.*,c.display_name AS customer_name FROM speaker_device_state s "
                + "LEFT JOIN speaker_customer c ON c.id=s.customer_id WHERE s.device_id=?", id);
        if (rows.isEmpty()) throw new SpeakerFailure(404, "DEVICE_NOT_FOUND");
        var result = publicDevice(rows.getFirst());
        result.put("events", db.queryForList("SELECT action,actor,created_at_ms FROM speaker_event "
                + "WHERE device_id=? ORDER BY created_at_ms DESC,id DESC LIMIT 50", id));
        return result;
    }
    private Map<String,Object> publicDevice(Map<String,Object> row) {
        var result = new LinkedHashMap<>(row);
        result.remove("credential_hash");
        result.put("online", online(row.get("last_seen_at_ms"), System.currentTimeMillis()));
        result.put("binding_state", row.get("customer_id") == null ? "unbound" : "bound");
        return result;
    }
    public Map<String,Object> register(String id, String name, String board, long operator) {
        settings.requireEnabled();
        if (id == null || !id.matches("[A-Za-z0-9_.:-]{1,128}") || name == null || name.isBlank()
                || name.length() > 128 || board != null && board.length() > 128)
            throw new SpeakerFailure(400, "INVALID_DEVICE");
        return transactions.execute(tx -> {
            if (!db.queryForList("SELECT device_id FROM dc_device WHERE device_id=? FOR UPDATE", id).isEmpty())
                throw new SpeakerFailure(409, "DEVICE_ALREADY_REGISTERED");
            String credential = token(); long now = System.currentTimeMillis();
            db.update("INSERT INTO dc_device(device_id,credential_hash) VALUES (?,?)", id, hash(credential));
            db.update("INSERT INTO speaker_device_state(device_id,name,board_type,created_at_ms) VALUES (?,?,?,?)",
                    id, name.strip(), board, now);
            event(id, "registered", "admin:" + operator);
            return Map.of("device_id", id, "credential", credential);
        });
    }
    public void event(String id, String action, String actor) {
        db.update("INSERT INTO speaker_event(id,device_id,action,actor,created_at_ms) VALUES (?,?,?,?,?)",
                UUID.randomUUID().toString(), id, action, actor, System.currentTimeMillis());
    }
}
