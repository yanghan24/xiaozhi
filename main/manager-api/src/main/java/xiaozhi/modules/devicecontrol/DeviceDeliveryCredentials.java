package xiaozhi.modules.devicecontrol;

import java.time.Clock;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.UUID;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import xiaozhi.common.user.UserDetail;

/** Revocable workstation credentials; raw secrets are returned only on issuance. */
@Service
public class DeviceDeliveryCredentials {
    private final JdbcTemplate db;
    private final Clock clock;
    private final boolean enabled;

    public DeviceDeliveryCredentials(JdbcTemplate db, @Qualifier("deviceControlClock") Clock clock,
            @Value("${device-delivery.enabled:false}") boolean enabled) {
        this.db = db;
        this.clock = clock;
        this.enabled = enabled;
    }

    Map<String, Object> issue(UserDetail user, String name) {
        long operator = operator(user);
        if (name == null || !name.matches("[\\p{L}\\p{N} _.-]{1,64}") || !name.equals(name.strip()) || name.isBlank()) {
            throw new ControlFailure(400, "INVALID_REQUEST");
        }
        String id = UUID.randomUUID().toString().replace("-", "");
        String token = DeviceDeliverySecrets.random();
        db.update("INSERT INTO dc_delivery_credential(credential_id,token_hash,operator_id,name,created_at_ms) VALUES (?,?,?,?,?)",
                id, DeviceDeliverySecrets.hash(token), operator, name, clock.millis());
        var result = metadata(db.queryForMap("SELECT * FROM dc_delivery_credential WHERE credential_id=?", id));
        result.put("tool_token", token);
        return result;
    }

    Map<String, Object> list(UserDetail user) {
        long operator = operator(user);
        var items = db.queryForList("SELECT * FROM dc_delivery_credential WHERE operator_id=? ORDER BY created_at_ms DESC,credential_id DESC LIMIT 200", operator)
                .stream().map(this::metadata).toList();
        return Map.of("credentials", items);
    }

    Map<String, Object> revoke(UserDetail user, String id) {
        long operator = operator(user);
        DeviceDeliveryService.validateOperationId(id);
        int updated = db.update("UPDATE dc_delivery_credential SET revoked_at_ms=COALESCE(revoked_at_ms,?) WHERE credential_id=? AND operator_id=?",
                clock.millis(), id, operator);
        if (updated == 0 && db.queryForList("SELECT credential_id FROM dc_delivery_credential WHERE credential_id=? AND operator_id=?", id, operator).isEmpty()) {
            throw new ControlFailure(404, "DELIVERY_CREDENTIAL_NOT_FOUND");
        }
        return metadata(db.queryForMap("SELECT * FROM dc_delivery_credential WHERE credential_id=? AND operator_id=?", id, operator));
    }

    Long authenticate(String hash) {
        if (!enabled) throw new ControlFailure(503, "DELIVERY_NOT_CONFIGURED");
        var rows = db.queryForList("SELECT operator_id FROM dc_delivery_credential WHERE token_hash=? AND revoked_at_ms IS NULL", hash);
        if (rows.isEmpty()) return null;
        // Recheck revocation in the UPDATE so a revoke between SELECT and UPDATE fails closed.
        if (db.update("UPDATE dc_delivery_credential SET last_used_at_ms=? WHERE token_hash=? AND revoked_at_ms IS NULL",
                clock.millis(), hash) == 0) return null;
        return ((Number) rows.getFirst().get("operator_id")).longValue();
    }

    private long operator(UserDetail user) {
        if (!enabled) throw new ControlFailure(503, "DELIVERY_NOT_CONFIGURED");
        if (user.getId() == null || !Integer.valueOf(1).equals(user.getSuperAdmin())
                || db.queryForList("SELECT id FROM sys_user WHERE id=? AND super_admin=1 AND status=1", user.getId()).isEmpty()) {
            throw new ControlFailure(403, "OPERATOR_REQUIRED");
        }
        return user.getId();
    }

    private LinkedHashMap<String, Object> metadata(Map<String, Object> row) {
        var result = new LinkedHashMap<String, Object>();
        for (String field : new String[] {"credential_id", "name", "created_at_ms", "revoked_at_ms", "last_used_at_ms"}) {
            result.put(field, row.get(field));
        }
        result.put("scope", "device_delivery");
        result.put("expires_at_ms", null);
        return result;
    }
}
