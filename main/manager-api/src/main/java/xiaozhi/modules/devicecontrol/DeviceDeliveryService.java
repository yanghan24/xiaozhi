package xiaozhi.modules.devicecontrol;

import java.net.URI;
import java.time.Clock;
import java.util.LinkedHashMap;
import java.util.Map;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.dao.DuplicateKeyException;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;
import xiaozhi.common.user.UserDetail;

/** One durable delivery operation spans issuance, USB writing and real device verification. */
@Service
public class DeviceDeliveryService {
    private static final long TOOL_LIFETIME_MS = 3600000;
    private final JdbcTemplate db;
    private final TransactionTemplate transactions;
    private final Clock clock;
    private final boolean enabled;
    private final String origin;
    private final DeviceDeliverySecrets secrets;
    private final DeviceDeliveryCredentials credentials;

    public DeviceDeliveryService(JdbcTemplate db, PlatformTransactionManager manager, DeviceDeliveryCredentials credentials,
            @Qualifier("deviceControlClock") Clock clock,
            @Value("${device-delivery.enabled:false}") boolean enabled,
            @Value("${device-control.enabled:false}") boolean controlEnabled,
            @Value("${device-control.public-origin:${PUBLIC_ORIGIN:}}") String origin,
            @Value("${device-delivery.encryption-key-file:}") String encryptionFile) {
        this.db = db;
        this.credentials = credentials;
        transactions = new TransactionTemplate(manager);
        this.clock = clock;
        this.enabled = enabled;
        this.origin = origin;
        secrets = enabled ? new DeviceDeliverySecrets(encryptionFile) : null;
        if (enabled) {
            if (!controlEnabled) throw new IllegalArgumentException("Device delivery requires device control");
            URI uri = URI.create(origin);
            if (!"https".equals(uri.getScheme()) || uri.getHost() == null || !uri.getRawPath().isEmpty()
                    || uri.getRawUserInfo() != null || uri.getRawQuery() != null || uri.getRawFragment() != null
                    || uri.getPort() == 0 || uri.getPort() > 65535 || !origin.matches("[\\x21-\\x7e]+")) {
                throw new IllegalArgumentException("Device delivery requires the configured HTTPS public origin");
            }
        }
    }

    Map<String, Object> authorize(UserDetail user) {
        requireEnabled();
        if (user.getId() == null || !Integer.valueOf(1).equals(user.getSuperAdmin())) {
            throw new ControlFailure(403, "OPERATOR_REQUIRED");
        }
        requireOperator(user.getId());
        String token = DeviceDeliverySecrets.random();
        long now = clock.millis();
        db.update("INSERT INTO dc_delivery_tool(token_hash,operator_id,created_at_ms,expires_at_ms) VALUES (?,?,?,?)",
                DeviceDeliverySecrets.hash(token), user.getId(), now, now + TOOL_LIFETIME_MS);
        return Map.of("tool_token", token, "expires_at_ms", now + TOOL_LIFETIME_MS);
    }

    long authenticateTool(String authorization) {
        requireEnabled();
        String hash = bearerHash(authorization, "TOOL_AUTH_REQUIRED");
        var rows = db.queryForList("SELECT operator_id FROM dc_delivery_tool WHERE token_hash=? AND expires_at_ms>?",
                hash, clock.millis());
        Long operator = rows.isEmpty() ? credentials.authenticate(hash) : Long.valueOf(number(rows.getFirst(), "operator_id"));
        if (operator == null) throw new ControlFailure(401, "TOOL_AUTH_REQUIRED");
        requireOperator(operator);
        return operator;
    }

    Map<String, Object> create(long operator, String operationId, String hardware, String networkHardware, String board) {
        requireEnabled();
        validateOperationId(operationId);
        validateHardware(hardware, networkHardware);
        if (board == null || !board.matches("[A-Za-z0-9_.:-]{1,128}")) throw new ControlFailure(400, "INVALID_REQUEST");
        try {
            return transactions.execute(tx -> {
                var existing = findOperation(operationId);
                if (existing != null) return replay(existing, operator, hardware, networkHardware, board);
                String device = "device_" + DeviceDeliverySecrets.random();
                String credential = DeviceDeliverySecrets.random();
                String hash = DeviceDeliverySecrets.hash(credential);
                db.update("INSERT INTO dc_device(device_id,credential_hash,admitted,reason) VALUES (?,?,FALSE,'device_disabled')",
                        device, hash);
                String context = operationId + ":" + device + ":" + operator;
                db.update("INSERT INTO dc_delivery_operation(operation_id,operator_id,device_id,hardware_id,network_hardware_id,board_type,credential_hash,credential_cipher,created_at_ms) VALUES (?,?,?,?,?,?,?,?,?)",
                        operationId, operator, device, hardware, networkHardware, board, hash,
                        secrets.encrypt(credential, context), clock.millis());
                return view(findOperation(operationId), true);
            });
        } catch (DuplicateKeyException competing) {
            // A failed concurrent INSERT rolls back the provisional dc_device too.
            var existing = findOperation(operationId);
            if (existing != null) return replay(existing, operator, hardware, networkHardware, board);
            throw new ControlFailure(409, "HARDWARE_ALREADY_REGISTERED");
        }
    }

    Map<String, Object> get(long operator, String operationId) {
        return view(ownedOperation(operator, operationId), false);
    }

    Map<String, Object> recover(UserDetail user, String operationId, String deviceId, String previousId) {
        requireEnabled();
        if (user.getId() == null || !Integer.valueOf(1).equals(user.getSuperAdmin())) {
            throw new ControlFailure(403, "OPERATOR_REQUIRED");
        }
        requireOperator(user.getId());
        validateOperationId(operationId);
        validateOperationId(previousId);
        if (deviceId == null || !deviceId.matches("[A-Za-z0-9_.:-]{1,128}") || operationId.equals(previousId)) {
            throw new ControlFailure(400, "INVALID_REQUEST");
        }
        return transactions.execute(tx -> {
            // Match binding, logout and voice gates: identity barrier before device before operation.
            db.queryForObject("SELECT id FROM customer_identity_barrier WHERE id=1 FOR UPDATE", Integer.class);
            var devices = db.queryForList("SELECT * FROM dc_device WHERE device_id=? FOR UPDATE", deviceId);
            if (devices.isEmpty()) throw new ControlFailure(404, "DELIVERY_NOT_FOUND");
            var device = devices.getFirst();
            var deliveries = db.queryForList("SELECT * FROM dc_delivery_operation WHERE device_id=? FOR UPDATE", deviceId);
            if (deliveries.isEmpty() || !"delivered".equals(deliveries.getFirst().get("status"))) {
                throw new ControlFailure(409, "STATE_CHANGED");
            }
            var delivery = deliveries.getFirst();
            var existing = db.queryForList("SELECT * FROM dc_device_recovery WHERE operation_id=? FOR UPDATE", operationId);
            if (!existing.isEmpty()) {
                var recovery = existing.getFirst();
                if (number(recovery, "operator_id") != user.getId() || !deviceId.equals(recovery.get("device_id"))
                        || !previousId.equals(recovery.get("previous_operation_id"))) {
                    throw new ControlFailure(409, "IDEMPOTENCY_CONFLICT");
                }
                return recoveryView(recovery, delivery, device);
            }
            if (findOperation(operationId) != null) throw new ControlFailure(409, "IDEMPOTENCY_CONFLICT");
            var previous = db.queryForList("SELECT * FROM dc_device_recovery WHERE operation_id=? AND device_id=?", previousId, deviceId);
            var source = previous.isEmpty() ? delivery : previous.getFirst();
            if (!previousId.equals(source.get("operation_id"))) throw new ControlFailure(409, "STATE_CHANGED");
            requireCurrentCredential(source, device);
            if (number(device, "revision") >= DeviceControlService.MAX_VERSION
                    || number(device, "permission_version") >= DeviceControlService.MAX_VERSION) {
                throw new ControlFailure(503, "TEMPORARILY_UNAVAILABLE");
            }
            String credential = DeviceDeliverySecrets.random();
            String hash = DeviceDeliverySecrets.hash(credential);
            db.update("INSERT INTO dc_device_recovery(operation_id,previous_operation_id,operator_id,device_id,credential_hash,credential_cipher,created_at_ms) VALUES (?,?,?,?,?,?,?)",
                    operationId, previousId, user.getId(), deviceId, hash,
                    secrets.encrypt(credential, operationId + ":" + deviceId + ":" + user.getId()), clock.millis());
            db.update("UPDATE dc_device SET credential_hash=?,revision=revision+1,permission_version=permission_version+1,current_request_id=NULL WHERE device_id=?", hash, deviceId);
            db.update("UPDATE dc_request SET status='replaced' WHERE device_id=? AND status='active'", deviceId);
            device.put("credential_hash", hash);
            return recoveryView(db.queryForMap("SELECT * FROM dc_device_recovery WHERE operation_id=?", operationId), delivery, device);
        });
    }

    private Map<String, Object> recoveryView(Map<String, Object> recovery, Map<String, Object> delivery, Map<String, Object> device) {
        requireCurrentCredential(recovery, device);
        var combined = new LinkedHashMap<String, Object>(delivery);
        combined.putAll(recovery);
        // Recovery pending is not initial admission and must not use initial revision checks.
        return identityView(combined, true);
    }

    Map<String, Object> recoveryReceipt(UserDetail user, String operationId, String writeId) {
        requireEnabled();
        if (user.getId() == null || !Integer.valueOf(1).equals(user.getSuperAdmin())) {
            throw new ControlFailure(403, "OPERATOR_REQUIRED");
        }
        requireOperator(user.getId());
        validateOperationId(operationId);
        if (writeId != null) validateOperationId(writeId);
        var selected = db.queryForList("SELECT * FROM dc_device_recovery WHERE operation_id=? AND operator_id=?", operationId, user.getId());
        if (selected.isEmpty()) throw new ControlFailure(404, "DELIVERY_NOT_FOUND");
        return transactions.execute(tx -> {
            var device = db.queryForMap("SELECT * FROM dc_device WHERE device_id=? FOR UPDATE", selected.getFirst().get("device_id"));
            var recovery = db.queryForMap("SELECT * FROM dc_device_recovery WHERE operation_id=? FOR UPDATE", operationId);
            requireCurrentCredential(recovery, device);
            if (writeId != null) {
                if ("delivered".equals(recovery.get("status"))) {
                    if (!writeId.equals(recovery.get("write_operation_id"))) throw new ControlFailure(409, "IDEMPOTENCY_CONFLICT");
                } else {
                    if (recovery.get("device_verified_at_ms") == null) throw new ControlFailure(409, "DEVICE_VERIFICATION_PENDING");
                    db.update("UPDATE dc_device_recovery SET status='delivered',write_operation_id=?,completed_at_ms=? WHERE operation_id=?",
                            writeId, clock.millis(), operationId);
                    recovery = db.queryForMap("SELECT * FROM dc_device_recovery WHERE operation_id=?", operationId);
                }
            }
            var combined = new LinkedHashMap<String, Object>(db.queryForMap("SELECT * FROM dc_delivery_operation WHERE device_id=?", device.get("device_id")));
            combined.putAll(recovery);
            return identityView(combined, false);
        });
    }

    Map<String, Object> checkDevice(String authorization, String deviceId, String hardware, String networkHardware) {
        requireEnabled();
        validateHardware(hardware, networkHardware);
        String hash = bearerHash(authorization, "INVALID_DEVICE_CREDENTIAL");
        if (deviceId == null || !deviceId.matches("[A-Za-z0-9_.:-]{1,128}")) {
            throw new ControlFailure(401, "INVALID_DEVICE_CREDENTIAL");
        }
        return transactions.execute(tx -> {
            var devices = db.queryForList("SELECT * FROM dc_device WHERE device_id=? FOR UPDATE", deviceId);
            if (devices.size() != 1 || !DeviceDeliverySecrets.sameHash(hash, String.valueOf(devices.getFirst().get("credential_hash")))) {
                throw new ControlFailure(401, "INVALID_DEVICE_CREDENTIAL");
            }
            var rows = db.queryForList("SELECT * FROM dc_delivery_operation WHERE device_id=? FOR UPDATE", deviceId);
            if (rows.size() != 1) throw new ControlFailure(403, "DELIVERY_NOT_FOUND");
            var operation = rows.getFirst();
            var device = devices.getFirst();
            var recoveries = db.queryForList("SELECT * FROM dc_device_recovery WHERE device_id=? AND credential_hash=? FOR UPDATE", deviceId, hash);
            if (!recoveries.isEmpty()) {
                var recovery = recoveries.getFirst();
                if (!hardware.equals(operation.get("hardware_id")) || !networkHardware.equals(operation.get("network_hardware_id"))) {
                    throw new ControlFailure(403, "HARDWARE_MISMATCH");
                }
                if (recovery.get("device_verified_at_ms") == null) {
                    db.update("UPDATE dc_device_recovery SET device_verified_at_ms=? WHERE operation_id=?", clock.millis(), recovery.get("operation_id"));
                }
                return Map.of("device_id", deviceId, "status", recovery.get("status"), "verified", true, "server_time_ms", clock.millis());
            }
            requireCurrentCredential(operation, device);
            if (!hardware.equals(operation.get("hardware_id")) || !networkHardware.equals(operation.get("network_hardware_id"))) {
                throw new ControlFailure(403, "HARDWARE_MISMATCH");
            }
            if ("delivered".equals(operation.get("status")) && !admitted(device)) {
                throw new ControlFailure(403, "DEVICE_NOT_ADMITTED");
            }
            if (operation.get("device_verified_at_ms") == null) {
                db.update("UPDATE dc_delivery_operation SET device_verified_at_ms=? WHERE operation_id=?",
                        clock.millis(), operation.get("operation_id"));
            }
            return Map.of("device_id", deviceId, "status", operation.get("status"), "verified", true,
                    "server_time_ms", clock.millis());
        });
    }

    Map<String, Object> complete(long operator, String operationId, String writeOperationId) {
        validateOperationId(writeOperationId);
        var selected = ownedOperation(operator, operationId);
        return transactions.execute(tx -> {
            // The device row is always locked first, as in device-check and customer operations.
            var device = db.queryForMap("SELECT * FROM dc_device WHERE device_id=? FOR UPDATE", selected.get("device_id"));
            var operation = db.queryForMap("SELECT * FROM dc_delivery_operation WHERE operation_id=? FOR UPDATE", operationId);
            requireCurrentCredential(operation, device);
            if ("delivered".equals(operation.get("status"))) {
                if (!writeOperationId.equals(operation.get("write_operation_id"))) throw new ControlFailure(409, "IDEMPOTENCY_CONFLICT");
                if (!admitted(device)) throw new ControlFailure(409, "STATE_CHANGED");
                return view(operation, false);
            }
            if (operation.get("device_verified_at_ms") == null) throw new ControlFailure(409, "DEVICE_VERIFICATION_PENDING");
            if (device.get("owner_subject") != null || admitted(device) || !"device_disabled".equals(device.get("reason"))) {
                throw new ControlFailure(409, "STATE_CHANGED");
            }
            if (number(device, "revision") != 1 || number(device, "permission_version") != 1) {
                throw new ControlFailure(409, "STATE_CHANGED");
            }
            long now = clock.millis();
            db.update("UPDATE dc_device SET admitted=TRUE,reason='unbound',revision=revision+1,permission_version=permission_version+1 WHERE device_id=?",
                    device.get("device_id"));
            db.update("UPDATE dc_delivery_operation SET status='delivered',write_operation_id=?,completed_at_ms=? WHERE operation_id=?",
                    writeOperationId, now, operationId);
            return view(findOperation(operationId), false);
        });
    }

    private Map<String, Object> replay(Map<String, Object> operation, long operator, String hardware, String networkHardware, String board) {
        if (number(operation, "operator_id") != operator || !hardware.equals(operation.get("hardware_id"))
                || !networkHardware.equals(operation.get("network_hardware_id")) || !board.equals(operation.get("board_type"))) {
            throw new ControlFailure(409, "IDEMPOTENCY_CONFLICT");
        }
        return view(operation, true);
    }

    private Map<String, Object> view(Map<String, Object> operation, boolean includeIdentity) {
        var device = db.queryForMap("SELECT * FROM dc_device WHERE device_id=?", operation.get("device_id"));
        requireCurrentCredential(operation, device);
        if (("delivered".equals(operation.get("status")) && !admitted(device))
                || ("pending".equals(operation.get("status"))
                    && (number(device, "revision") != 1 || number(device, "permission_version") != 1))) {
            throw new ControlFailure(409, "STATE_CHANGED");
        }
        return identityView(operation, includeIdentity);
    }

    private Map<String, Object> identityView(Map<String, Object> operation, boolean includeIdentity) {
        var result = new LinkedHashMap<String, Object>();
        for (String field : new String[] {"operation_id", "device_id", "hardware_id", "network_hardware_id", "board_type", "status", "device_verified_at_ms", "completed_at_ms"}) {
            result.put(field, operation.get(field));
        }
        if (includeIdentity) {
            String context = operation.get("operation_id") + ":" + operation.get("device_id") + ":" + operation.get("operator_id");
            String credential = secrets.decrypt((String) operation.get("credential_cipher"), context);
            if (!credential.matches("[A-Za-z0-9_-]{43}")
                    || !DeviceDeliverySecrets.sameHash(DeviceDeliverySecrets.hash(credential), (String) operation.get("credential_hash"))) {
                throw new ControlFailure(503, "DELIVERY_STORAGE_UNAVAILABLE");
            }
            result.put("identity", Map.of("version", 1, "hardware_id", operation.get("hardware_id"),
                    "device_id", operation.get("device_id"), "credential", credential,
                    "ota_url", origin + "/xiaozhi/ota/", "control_origin", ""));
        }
        return result;
    }

    private Map<String, Object> ownedOperation(long operator, String operationId) {
        requireEnabled();
        validateOperationId(operationId);
        var operation = findOperation(operationId);
        if (operation == null || number(operation, "operator_id") != operator) throw new ControlFailure(404, "DELIVERY_NOT_FOUND");
        return operation;
    }

    private Map<String, Object> findOperation(String id) {
        var rows = db.queryForList("SELECT * FROM dc_delivery_operation WHERE operation_id=?", id);
        return rows.isEmpty() ? null : rows.getFirst();
    }

    private static void requireCurrentCredential(Map<String, Object> operation, Map<String, Object> device) {
        if (!DeviceDeliverySecrets.sameHash((String) operation.get("credential_hash"), String.valueOf(device.get("credential_hash")))) {
            throw new ControlFailure(409, "STATE_CHANGED");
        }
    }

    private void requireOperator(long userId) {
        if (db.queryForList("SELECT id FROM sys_user WHERE id=? AND super_admin=1 AND status=1", userId).size() != 1) {
            throw new ControlFailure(403, "OPERATOR_REQUIRED");
        }
    }

    private void requireEnabled() {
        if (!enabled) throw new ControlFailure(503, "DELIVERY_NOT_CONFIGURED");
    }

    static void validateOperationId(String id) {
        if (id == null || !id.matches("[0-9a-f]{32}")) throw new ControlFailure(400, "INVALID_REQUEST");
    }

    private static void validateHardware(String hardware, String networkHardware) {
        if (hardware == null || networkHardware == null || !hardware.matches("[0-9a-f]{12}")
                || !networkHardware.matches("[0-9a-f]{12}") || hardware.equals(networkHardware)) {
            throw new ControlFailure(400, "INVALID_REQUEST");
        }
    }

    private static String bearerHash(String authorization, String code) {
        if (authorization == null || !authorization.matches("Bearer [A-Za-z0-9_-]{43,128}")) throw new ControlFailure(401, code);
        return DeviceDeliverySecrets.hash(authorization.substring(7));
    }

    private static long number(Map<String, Object> row, String key) { return ((Number) row.get(key)).longValue(); }
    private static boolean admitted(Map<String, Object> device) {
        return Boolean.TRUE.equals(device.get("admitted")) || Integer.valueOf(1).equals(device.get("admitted"));
    }
}
