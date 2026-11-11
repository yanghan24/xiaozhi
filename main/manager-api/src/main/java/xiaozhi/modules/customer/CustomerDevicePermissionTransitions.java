package xiaozhi.modules.customer;

import java.util.Map;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Component;

/** Shared local-permission writes; callers own authentication, locks and transactions. */
@Component
final class CustomerDevicePermissionTransitions {
    private final JdbcTemplate db;

    CustomerDevicePermissionTransitions(JdbcTemplate db) {
        this.db = db;
    }

    /** Requires the identity barrier and device row lock in the caller's transaction. */
    void disable(Map<String,Object> device, String reason) {
        long permission = next(device, "permission_version");
        long revision = next(device, "revision");
        db.update("UPDATE dc_device SET local_enabled=false,reason=?,permission_version=?,revision=?,current_request_id=NULL WHERE device_id=?",
                reason, permission, revision, device.get("device_id"));
        db.update("UPDATE dc_request SET status='replaced' WHERE device_id=? AND status='active'", device.get("device_id"));
        device.put("local_enabled", false);
        device.put("reason", reason);
        device.put("permission_version", permission);
        device.put("revision", revision);
        device.put("current_request_id", null);
    }

    private static long next(Map<String,Object> device, String field) {
        long value = CustomerIdentityService.number(device, field);
        if (value >= 9007199254740991L) throw new CustomerFailure(503, "TEMPORARILY_UNAVAILABLE");
        return value + 1;
    }
}
