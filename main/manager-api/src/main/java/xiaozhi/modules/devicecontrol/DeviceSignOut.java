package xiaozhi.modules.devicecontrol;

import org.springframework.jdbc.core.JdbcTemplate;

/** Shared state transition for the phone page and the device's own settings. */
public final class DeviceSignOut {
    private DeviceSignOut() {}

    /** Caller holds the identity barrier and the device row lock in its transaction. */
    public static void store(JdbcTemplate db, String deviceId, long permissionVersion, long revision) {
        db.update("UPDATE dc_request SET status='replaced' WHERE device_id=? AND status='active'", deviceId);
        db.update("UPDATE dc_device SET local_enabled=false,reason='user_signed_out',permission_version=?,"
                + "revision=?,current_request_id=NULL WHERE device_id=?", permissionVersion, revision, deviceId);
    }
}
