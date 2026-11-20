package xiaozhi.modules.customer;

import com.fasterxml.jackson.databind.JsonNode;
import java.time.Clock;
import java.time.Duration;
import java.util.Map;
import java.util.Optional;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;

/** All operations run under the existing authoritative device/ownership lock. */
@Service
public class CustomerReminders {
    private static final long LATE_MS = Duration.ofMinutes(10).toMillis();
    private static final String OWNER = "device_id=? AND ownership_version=?";
    private final JdbcTemplate db;
    private final Clock clock;
    private final CustomerOidcConfig secrets;

    CustomerReminders(JdbcTemplate db, @Qualifier("customerTime") Clock clock, CustomerOidcConfig secrets) {
        this.db = db;
        this.clock = clock;
        this.secrets = secrets;
    }

    public Optional<String> nextDue(Map<String,Object> device) {
        long now = clock.millis();
        return db.queryForList("SELECT id FROM dc_reminder WHERE " + OWNER
                + " AND status='pending' AND due_at_ms BETWEEN ? AND ? ORDER BY due_at_ms,id LIMIT 1",
                String.class, device.get("device_id"), device.get("ownership_version"), now - LATE_MS, now)
                .stream().findFirst();
    }

    Map<String,Object> handle(Map<String,Object> device, String session, JsonNode command) {
        Object id = device.get("device_id");
        Object owner = device.get("ownership_version");
        String action = command.path("action").asText();
        long now = clock.millis();
        if ("list".equals(action)) {
            int offset = 0;
            if (command.size() != 1) {
                var start = command.path("offset");
                if (command.size() != 2 || !start.isIntegralNumber() || !start.canConvertToInt() || start.intValue() < 0)
                    throw invalid();
                offset = start.intValue();
            }
            var entries = db.queryForList("SELECT id,version,due_at_ms,title_cipher FROM dc_reminder WHERE "
                    + OWNER + " AND status='pending' AND due_at_ms>=? ORDER BY due_at_ms,id LIMIT 6 OFFSET ?",
                    id, owner, now - LATE_MS, offset);
            boolean more = entries.size() > 5;
            if (more) entries.removeLast();
            for (var entry : entries) entry.put("title", secrets.decrypt((String)entry.remove("title_cipher")));
            return Map.of("items", entries, "more", more);
        }
        String key = command.path("id").asText();
        if (!key.matches("[a-f0-9]{32}")) throw invalid();
        if ("claim".equals(action) && command.size() == 2) {
            int claimed = db.update("UPDATE dc_reminder SET status='unknown',claimed_session=?,ended_at_ms=? WHERE id=? AND "
                    + OWNER + " AND status='pending' AND due_at_ms BETWEEN ? AND ?", CustomerSecrets.hash(session),
                    now, key, id, owner, now - LATE_MS, now);
            if (claimed == 0) return Map.of("status", "unavailable");
            String title = db.queryForObject("SELECT title_cipher FROM dc_reminder WHERE id=?", String.class, key);
            return Map.of("status", "claimed", "title", secrets.decrypt(title));
        }
        if ("played".equals(action) && command.size() == 2) {
            int updated = db.update("UPDATE dc_reminder SET status='played',ended_at_ms=? WHERE id=? AND "
                    + OWNER + " AND status='unknown' AND claimed_session=?", now, key, id, owner, CustomerSecrets.hash(session));
            return Map.of("status", updated == 1 ? "played" : "unavailable");
        }
        if ("cancel".equals(action) && command.size() == 3) {
            int updated = db.update("UPDATE dc_reminder SET status='cancelled',ended_at_ms=?,version=version+1 WHERE id=? AND "
                    + OWNER + " AND status='pending' AND version=?", now, key, id, owner, version(command));
            return Map.of("status", updated == 1 ? "cancelled" : "changed");
        }
        boolean create = "create".equals(action) && command.size() == 4;
        if (!create && !("update".equals(action) && command.size() == 5)) throw invalid();
        JsonNode due = command.path("due_at_ms");
        String title = command.path("title").textValue();
        if (!due.isIntegralNumber() || !due.canConvertToLong() || due.longValue() <= now
                || due.longValue() > 253402300799000L || title == null || title.isBlank()
                || title.length() > 80 || title.chars().anyMatch(Character::isISOControl)) throw invalid();
        if (create) {
            var existing = db.queryForList("SELECT title_cipher,due_at_ms,version,status FROM dc_reminder WHERE id=? AND " + OWNER, key, id, owner);
            if (!existing.isEmpty()) {
                var row = existing.getFirst();
                boolean same = ((Number)row.get("version")).longValue() == 1 && row.get("status").equals("pending")
                        && ((Number)row.get("due_at_ms")).longValue() == due.longValue()
                        && title.equals(secrets.decrypt((String)row.get("title_cipher")));
                return Map.of("status", same ? "saved" : "changed");
            }
            db.update("INSERT INTO dc_reminder (id,device_id,ownership_version,due_at_ms,title_cipher,status) VALUES (?,?,?,?,?,'pending')",
                    key, id, owner, due.longValue(), secrets.encrypt(title));
            return Map.of("status", "saved");
        }
        int updated = db.update("UPDATE dc_reminder SET title_cipher=?,due_at_ms=?,version=version+1 WHERE id=? AND "
                + OWNER + " AND status='pending' AND version=?", secrets.encrypt(title), due.longValue(), key, id, owner, version(command));
        return Map.of("status", updated == 1 ? "saved" : "changed");
    }

    private static long version(JsonNode command) {
        JsonNode version = command.path("version");
        if (!version.isIntegralNumber() || !version.canConvertToLong() || version.longValue() < 1) throw invalid();
        return version.longValue();
    }

    private static CustomerFailure invalid() { return new CustomerFailure(400, "INVALID_REQUEST"); }

    @Scheduled(fixedDelay = 60000)
    void expire() {
        long now = clock.millis();
        db.update("UPDATE dc_reminder SET status='missed',ended_at_ms=due_at_ms+? WHERE status='pending' AND due_at_ms<?",
                LATE_MS, now - LATE_MS);
        db.update("DELETE FROM dc_reminder WHERE ended_at_ms<?", now - Duration.ofDays(30).toMillis());
    }
}
