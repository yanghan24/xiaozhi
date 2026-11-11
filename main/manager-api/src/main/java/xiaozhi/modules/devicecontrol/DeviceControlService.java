package xiaozhi.modules.devicecontrol;

import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.time.Clock;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Base64;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;

import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;

/** Authoritative product state; no upstream administrator identity or voice token is reused. */
@Service
public class DeviceControlService {
    static final long MAX_VERSION = 9007199254740991L;
    private final SecureRandom random = new SecureRandom();
    private final Semaphore waiters = new Semaphore(64);
    private final Semaphore accountReaders = new Semaphore(8);
    private final JdbcTemplate db;
    private final TransactionTemplate transactions;
    private final Clock clock;
    private final boolean enabled;
    private final String publicOrigin;
    private final xiaozhi.modules.customer.CustomerVoicePermissions permissions;
    private final xiaozhi.modules.customer.CustomerAccountDetails accountDetails;

    public DeviceControlService(JdbcTemplate db, PlatformTransactionManager transactionManager,
            @Qualifier("deviceControlClock") Clock clock,
            @Value("${device-control.enabled:false}") boolean enabled,
            @Value("${device-control.public-origin:${PUBLIC_ORIGIN:}}") String publicOrigin,
            xiaozhi.modules.customer.CustomerVoicePermissions permissions,
            xiaozhi.modules.customer.CustomerAccountDetails accountDetails) {
        this.db = db; this.permissions=permissions;
        this.accountDetails = accountDetails;
        this.transactions = new TransactionTemplate(transactionManager);
        this.clock = clock;
        this.enabled = enabled;
        this.publicOrigin = publicOrigin;
        if (enabled) {
            URI origin = URI.create(publicOrigin);
            if (!"https".equals(origin.getScheme()) || origin.getHost() == null
                    || origin.getRawUserInfo() != null || origin.getRawQuery() != null
                    || origin.getRawFragment() != null || !origin.getRawPath().isEmpty()
                    || origin.getPort() == 0 || origin.getPort() > 65535
                    || !publicOrigin.matches("[\\x21-\\x7e]+")
                    || publicOrigin.length() + "/xiaozhi/bind/".length() + 43 > 512) {
                throw new IllegalArgumentException("device-control.public-origin must be an HTTPS origin fitting the QR limit");
            }
        }
    }

    record Identity(String deviceId, String hash) { }
    private record AccountOwner(String issuer, String subject, long ownership, long permission) {}

    Identity authenticate(String authorization, String deviceId) {
        if (!enabled) throw new ControlFailure(503, "TEMPORARILY_UNAVAILABLE");
        if (authorization == null || !authorization.matches("Bearer [A-Za-z0-9_-]{43,128}")
                || deviceId == null || !deviceId.matches("[A-Za-z0-9_.:-]{1,128}")) {
            throw new ControlFailure(401, "INVALID_DEVICE_CREDENTIAL");
        }
        var identity = new Identity(deviceId, hash(authorization.substring(7)));
        load(identity, false);
        return identity;
    }

    private Map<String, Object> load(Identity identity, boolean lock) {
        var rows = db.queryForList("SELECT * FROM dc_device WHERE credential_hash=?" +
                (lock ? " FOR UPDATE" : ""), identity.hash());
        if (rows.size() != 1 || !identity.deviceId().equals(rows.getFirst().get("device_id"))) {
            throw new ControlFailure(401, "INVALID_DEVICE_CREDENTIAL");
        }
        var row = rows.getFirst();
        if (!Boolean.TRUE.equals(row.get("admitted")) && !Integer.valueOf(1).equals(row.get("admitted"))) {
            throw new ControlFailure(403, "DEVICE_NOT_ADMITTED");
        }
        return row;
    }

    Map<String, Object> state(Identity identity) {
        return transactions.execute(tx -> {
            permissions.barrier();
            var row = load(identity, true);
            permissions.reconcile(row);
            long responseNow = clock.millis();
            var current = currentRequest(row, responseNow);
            var state = new LinkedHashMap<String, Object>();
            state.put("device_id", identity.deviceId());
            state.put("revision", row.get("revision"));
            state.put("permission_version", row.get("permission_version"));
            state.put("server_time_ms", responseNow);
            state.put("binding_state", row.get("owner_subject") == null ? "unbound" : "bound");
            var voice=permissions.voice(row,responseNow);
            state.put("use_permission", voice==null?"blocked":"allowed");
            state.put("reason", row.get("reason"));
            state.put("binding_request", current == null ? null : requestBody(current, responseNow));
            state.put("lease", voice==null?null:Map.of("permission_version",row.get("permission_version"),"expires_at_ms",responseNow+60000,"expires_in_ms",60000));
            state.put("voice", voice);
            return state;
        });
    }

    private AccountOwner accountOwner(Identity identity) {
        return transactions.execute(tx -> {
            permissions.barrier();
            var row = load(identity, true);
            permissions.reconcile(row);
            if (row.get("owner_subject") == null || !"ready".equals(row.get("reason")))
                throw new ControlFailure(409, "STATE_CHANGED");
            return new AccountOwner((String) row.get("owner_issuer"), (String) row.get("owner_subject"),
                    number(row, "ownership_version"), number(row, "permission_version"));
        });
    }

    Map<String, Object> account(Identity identity) {
        if (!accountReaders.tryAcquire()) throw new ControlFailure(429, "RATE_LIMITED");
        try {
            var owner = accountOwner(identity);
            // The market request must not hold a device lock or the shared identity barrier.
            var details = accountDetails.read(owner.issuer(), owner.subject());
            if (!owner.equals(accountOwner(identity))) throw new ControlFailure(409, "STATE_CHANGED");
            var data = new LinkedHashMap<String, Object>();
            data.put("device_id", identity.deviceId());
            data.put("permission_version", owner.permission());
            data.put("username", details.username());
            data.put("balance", details.balance());
            return data;
        } finally {
            accountReaders.release();
        }
    }

    Map<String, Object> signOut(Identity identity, long expected) {
        return transactions.execute(tx -> {
            permissions.barrier();
            var row = load(identity, true);
            long permission = number(row, "permission_version");
            long revision = number(row, "revision");
            if (row.get("owner_subject") == null || "device_disabled".equals(row.get("reason")))
                throw new ControlFailure(409, "STATE_CHANGED");
            if ("user_signed_out".equals(row.get("reason")) && permission == expected + 1)
                return Map.of("permission_version", permission);
            if (permission != expected || "user_signed_out".equals(row.get("reason")))
                throw new ControlFailure(409, "STATE_CHANGED");
            if (permission >= MAX_VERSION || revision >= MAX_VERSION)
                throw new ControlFailure(503, "TEMPORARILY_UNAVAILABLE");
            DeviceSignOut.store(db, identity.deviceId(), permission + 1, revision + 1);
            return Map.of("permission_version", permission + 1);
        });
    }

    Map<String, Object> unbind(Identity identity, long expected) {
        if (expected < 1) throw new ControlFailure(400, "INVALID_REQUEST");
        return transactions.execute(tx -> {
            permissions.barrier();
            var row = load(identity, true);
            long permission = number(row, "permission_version");
            long revision = number(row, "revision");
            long ownership = number(row, "ownership_version");
            // An acknowledged response may be lost. Only the exact unbound successor
            // satisfies that retry; a later binding must never be changed by it.
            if (row.get("owner_subject") == null && "unbound".equals(row.get("reason"))
                    && permission == expected + 1)
                return Map.of("permission_version", permission, "binding_state", "unbound");
            if (permission != expected || row.get("owner_subject") == null
                    || "device_disabled".equals(row.get("reason")))
                throw new ControlFailure(409, "STATE_CHANGED");
            if (permission >= MAX_VERSION || revision >= MAX_VERSION || ownership >= MAX_VERSION)
                throw new ControlFailure(503, "TEMPORARILY_UNAVAILABLE");
            db.update("UPDATE dc_request SET status='replaced' WHERE device_id=? AND status='active'",
                    identity.deviceId());
            db.update("UPDATE dc_device SET owner_issuer=NULL,owner_subject=NULL,ownership_version=?,"
                    + "local_enabled=false,permission_authorization_order=0,reason='unbound',"
                    + "permission_version=?,revision=?,current_request_id=NULL WHERE device_id=?",
                    ownership + 1, permission + 1, revision + 1, identity.deviceId());
            return Map.of("permission_version", permission + 1, "binding_state", "unbound");
        });
    }

    Map<String, Object> poll(Identity identity, Long afterRevision, int waitMs) {
        var snapshot = state(identity);
        if (afterRevision == null || waitMs == 0 || number(snapshot, "revision") != afterRevision) return snapshot;
        if (!waiters.tryAcquire()) throw new ControlFailure(429, "RATE_LIMITED");
        long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(waitMs);
        try {
            // The database is the durable cross-instance notification source. No transaction or
            // connection is held while sleeping, and each read rechecks credential revocation.
            while (number(snapshot, "revision") == afterRevision) {
                long remaining = deadline - System.nanoTime();
                if (remaining <= 0) break;
                TimeUnit.NANOSECONDS.sleep(Math.min(remaining, TimeUnit.MILLISECONDS.toNanos(200)));
                snapshot = state(identity);
            }
            return snapshot;
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new ControlFailure(503, "TEMPORARILY_UNAVAILABLE");
        } finally {
            waiters.release();
        }
    }

    Map<String, Object> createRequest(Identity identity, String key, String intent, long expectedVersion) {
        if (key == null || !key.matches("[A-Za-z0-9_.:-]{1,128}")) {
            throw new ControlFailure(400, "INVALID_REQUEST");
        }
        return transactions.execute(tx -> {
            permissions.barrier();
            var device = load(identity, true);
            permissions.reconcile(device);
            var operations = db.queryForList("SELECT * FROM dc_request_operation WHERE device_id=? AND key_hash=?",
                    identity.deviceId(), hash(key));
            if (!operations.isEmpty()) {
                var operation = operations.getFirst();
                if (!intent.equals(operation.get("intent"))
                        || expectedVersion != number(operation, "expected_permission_version")) {
                    throw new ControlFailure(409, "IDEMPOTENCY_CONFLICT");
                }
            }
            if (expectedVersion != number(device, "permission_version")) {
                throw new ControlFailure(409, "STATE_CHANGED");
            }
            if ("device_disabled".equals(device.get("reason"))) {
                throw new ControlFailure(403, "DEVICE_NOT_ADMITTED");
            }
            boolean bound = device.get("owner_subject") != null;
            if ("account".equals(intent) && !bound) throw new ControlFailure(409, "STATE_CHANGED");
            if("auto".equals(intent) && "ready".equals(device.get("reason"))) throw new ControlFailure(409,"REQUEST_NOT_NEEDED");
            String purpose = "account".equals(intent) ? "account" : bound ? "resume" : "bind";
            var current = currentRequest(device, clock.millis());
            if (!operations.isEmpty()) {
                var previous = db.queryForMap("SELECT * FROM dc_request WHERE request_id=?",
                        operations.getFirst().get("request_id"));
                String status = (String) previous.get("status");
                if ("replaced".equals(status) || "consumed".equals(status)) {
                    throw new ControlFailure(409, "REQUEST_CONSUMED");
                }
                if (number(previous, "expires_at_ms") <= clock.millis()) {
                    throw new ControlFailure(410, "REQUEST_EXPIRED");
                }
                if (current == null || !previous.get("request_id").equals(current.get("request_id"))
                        || !purpose.equals(previous.get("purpose"))) {
                    throw new ControlFailure(409, "STATE_CHANGED");
                }
                return requestResponse(device, previous);
            }
            if (current == null || !purpose.equals(current.get("purpose"))
                    || number(current, "permission_version") != expectedVersion) {
                if (current != null) {
                    db.update("UPDATE dc_request SET status='replaced' WHERE request_id=?", current.get("request_id"));
                }
                long now = clock.millis();
                String id = randomValue();
                String handle = randomValue();
                db.update("INSERT INTO dc_request(request_id,device_id,permission_version,purpose,verification_uri,created_at_ms,expires_at_ms) VALUES (?,?,?,?,?,?,?)",
                        id, identity.deviceId(), expectedVersion, purpose,
                        publicOrigin + "/xiaozhi/bind/" + handle, now, now + 600000);
                db.update("INSERT INTO customer_device_request(handle,control_request_id,control_credential_hash,purpose,permission_version,expires_at_ms) VALUES (?,?,?,?,?,?)",
                        handle, id, identity.hash(), purpose, expectedVersion, now + 600000);
                bumpRevision(device);
                db.update("UPDATE dc_device SET current_request_id=? WHERE device_id=?", id, identity.deviceId());
                device.put("current_request_id", id);
                current = db.queryForMap("SELECT * FROM dc_request WHERE request_id=?", id);
            }
            db.update("INSERT INTO dc_request_operation(device_id,key_hash,intent,expected_permission_version,request_id,created_at_ms) VALUES (?,?,?,?,?,?)",
                    identity.deviceId(), hash(key), intent, expectedVersion, current.get("request_id"), clock.millis());
            return requestResponse(device, current);
        });
    }

    private Map<String, Object> currentRequest(Map<String, Object> device, long now) {
        if (device.get("current_request_id") == null) return null;
        var current = db.queryForMap("SELECT * FROM dc_request WHERE request_id=? AND device_id=?",
                device.get("current_request_id"), device.get("device_id"));
        boolean expired = number(current, "expires_at_ms") <= now;
        boolean stale = number(current, "permission_version") != number(device, "permission_version")
                || "device_disabled".equals(device.get("reason"));
        if (expired || stale || !"active".equals(current.get("status"))) {
            if ("active".equals(current.get("status"))) {
                db.update("UPDATE dc_request SET status=? WHERE request_id=?",
                        stale ? "replaced" : "expired", current.get("request_id"));
            }
            bumpRevision(device);
            db.update("UPDATE dc_device SET current_request_id=NULL WHERE device_id=?", device.get("device_id"));
            device.put("current_request_id", null);
            return null;
        }
        return current;
    }

    Map<String, Object> acknowledge(Identity identity, long revision, long permissionVersion, boolean audioStopped) {
        return transactions.execute(tx -> {
            permissions.barrier();
            var device = load(identity, true);
            permissions.reconcile(device);
            Map<String, Object> result = Map.of("accepted", true, "revision", revision, "permission_version", permissionVersion);
            var history = db.queryForList("SELECT 1 FROM dc_state_ack_history WHERE device_id=? AND revision=? AND permission_version=? AND audio_stopped=?",
                    identity.deviceId(), revision, permissionVersion, audioStopped);
            // A historical success acknowledges only its original version. Do not touch the
            // current device state, latest ACK, or receive timestamp when replaying it.
            if (!history.isEmpty()) return result;
            currentRequest(device, clock.millis());
            if (revision != number(device, "revision") || permissionVersion != number(device, "permission_version")) {
                throw new ControlFailure(409, "STATE_CHANGED");
            }
            if (!audioStopped && !"ready".equals(device.get("reason"))) throw new ControlFailure(400, "INVALID_REQUEST");
            long receivedAt = clock.millis();
            db.update("INSERT INTO dc_state_ack_history(device_id,revision,permission_version,audio_stopped,received_at_ms) VALUES (?,?,?,?,?)",
                    identity.deviceId(), revision, permissionVersion, audioStopped, receivedAt);
            db.update("INSERT INTO dc_state_ack(device_id,revision,permission_version,audio_stopped,received_at_ms) VALUES (?,?,?,?,?) "
                    + "ON DUPLICATE KEY UPDATE received_at_ms=IF(revision=VALUES(revision) AND permission_version=VALUES(permission_version),received_at_ms,VALUES(received_at_ms)), "
                    + "revision=VALUES(revision),permission_version=VALUES(permission_version),audio_stopped=VALUES(audio_stopped)",
                    identity.deviceId(), revision, permissionVersion, audioStopped, receivedAt);
            return result;
        });
    }

    private void bumpRevision(Map<String, Object> device) {
        long revision = number(device, "revision");
        if (revision >= MAX_VERSION) throw new ControlFailure(503, "TEMPORARILY_UNAVAILABLE");
        db.update("UPDATE dc_device SET revision=? WHERE device_id=?", revision + 1, device.get("device_id"));
        device.put("revision", revision + 1);
    }

    private Map<String, Object> requestResponse(Map<String, Object> device, Map<String, Object> request) {
        long responseNow = clock.millis();
        // Recheck at response generation: persistence work may have crossed the deadline.
        if (number(request, "expires_at_ms") <= responseNow) throw new ControlFailure(410, "REQUEST_EXPIRED");
        return Map.of("revision", device.get("revision"), "permission_version", device.get("permission_version"),
                "server_time_ms", responseNow, "binding_request", requestBody(request, responseNow));
    }

    private Map<String, Object> requestBody(Map<String, Object> request, long responseNow) {
        return Map.of("request_id", request.get("request_id"), "purpose", request.get("purpose"),
                "verification_uri", request.get("verification_uri"), "created_at_ms", request.get("created_at_ms"),
                "expires_at_ms", request.get("expires_at_ms"),
                "expires_in_ms", number(request, "expires_at_ms") - responseNow);
    }

    private static long number(Map<String, Object> row, String key) {
        return ((Number) row.get(key)).longValue();
    }

    private String randomValue() {
        byte[] bytes = new byte[32];
        random.nextBytes(bytes);
        return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);
    }

    private static String hash(String value) {
        try {
            return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256")
                    .digest(value.getBytes(StandardCharsets.UTF_8)));
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException(impossible);
        }
    }
}
