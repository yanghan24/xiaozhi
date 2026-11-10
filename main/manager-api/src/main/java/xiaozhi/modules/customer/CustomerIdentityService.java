package xiaozhi.modules.customer;

import org.springframework.beans.factory.annotation.Qualifier;

import java.util.Map;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;

@Service
final class CustomerIdentityService {
    final JdbcTemplate db;
    private final java.time.Clock clock;
    private final CustomerMarket market;
    private final CustomerOidcConfig config;
    private final TransactionTemplate transactions;
    private final CustomerVoicePermissions voice;
    CustomerIdentityService(JdbcTemplate db, CustomerMarket market, CustomerOidcConfig config, PlatformTransactionManager manager, @Qualifier("customerTime") java.time.Clock clock, CustomerVoicePermissions voice) {
        this.clock = clock; this.voice=voice;
        this.db = db; this.market = market; this.config = config;
        transactions = new TransactionTemplate(manager);
        transactions.setIsolationLevel(org.springframework.transaction.TransactionDefinition.ISOLATION_READ_COMMITTED);
    }
    Map<String, Object> browser(String cookie) {
        if (cookie == null || cookie.length() > 64) throw new CustomerFailure(401, "IDENTITY_REQUIRED");
        var rows = db.queryForList("SELECT * FROM customer_browser WHERE id_hash=?", CustomerSecrets.hash(cookie));
        if (rows.isEmpty() || number(rows.getFirst(), "last_activity_ms") + 1_800_000 <= clock.millis())
            throw new CustomerFailure(401, "IDENTITY_REQUIRED");
        return rows.getFirst();
    }
    Map<String, Object> request(String handle) {
        var rows = db.queryForList("SELECT r.*,COALESCE(d.alias,'小智设备') AS alias,cr.status AS control_status,"
                + "cr.device_id AS control_device_id,cd.current_request_id,cd.permission_version AS current_permission_version,"
                + "cd.credential_hash AS current_credential_hash,cd.admitted,cd.reason "
                + "FROM customer_device_request r LEFT JOIN ai_device d ON d.id=r.device_id "
                + "LEFT JOIN dc_request cr ON cr.request_id=r.control_request_id "
                + "LEFT JOIN dc_device cd ON cd.device_id=cr.device_id WHERE r.handle=?", handle);
        if (rows.isEmpty()) throw new CustomerFailure(404, "REQUEST_NOT_FOUND");
        var row = rows.getFirst();
        if (number(row, "expires_at_ms") <= clock.millis()) throw new CustomerFailure(410, "REQUEST_EXPIRED");
        if (Boolean.TRUE.equals(row.get("consumed"))) throw new CustomerFailure(409, "REQUEST_CONSUMED");
        if (row.get("control_request_id") != null && (!"active".equals(row.get("control_status"))
                || !row.get("control_request_id").equals(row.get("current_request_id"))
                || !Boolean.TRUE.equals(row.get("admitted")) || "device_disabled".equals(row.get("reason"))
                || row.get("control_credential_hash") == null
                || !row.get("control_credential_hash").equals(row.get("current_credential_hash"))
                || number(row,"permission_version") != number(row,"current_permission_version")))
            throw new CustomerFailure(409,"STATE_CHANGED");
        return row;
    }
    Map<String, Object> createFlow(String cookie, String csrf, String origin, String handle) {
        Map<String, Object> browser;
        try { browser = browser(cookie); } catch (CustomerFailure e) { throw new CustomerFailure(403, "CSRF_REJECTED"); }
        if (!CustomerOidcConfig.ORIGIN.equals(origin) || csrf == null || !csrf.equals(browser.get("csrf")))
            throw new CustomerFailure(403, "CSRF_REJECTED");
        var request = request(handle);
        String id = CustomerSecrets.random(), state = CustomerSecrets.random(), nonce = CustomerSecrets.random(), verifier = CustomerSecrets.random();
        String url = market.authorizationUrl(state, nonce, verifier);
        long now = clock.millis();
        db.update("INSERT INTO customer_identity_flow(id,browser_hash,request_handle,purpose,permission_version,state_hash,nonce,verifier,expires_at_ms,status,created_at_ms) VALUES (?,?,?,?,?,?,?,?,?,'AWAITING_IDENTITY',?)",
                id, browser.get("id_hash"), handle, request.get("purpose"), request.get("permission_version"), CustomerSecrets.hash(state), nonce,
                config.encrypt(verifier), Math.min(now + 300_000, number(request, "expires_at_ms")), now);
        db.update("UPDATE customer_browser SET last_activity_ms=? WHERE id_hash=?", now, browser.get("id_hash"));
        return Map.of("flow_id", id, "authorization_url", url);
    }
    Map<String, Object> flowStatus(String cookie, String id) {
        var browser = browser(cookie);
        var rows = db.queryForList("SELECT f.*,COALESCE(d.alias,'小智设备') AS alias,r.expires_at_ms AS request_expires_at_ms,r.consumed FROM customer_identity_flow f JOIN customer_device_request r ON r.handle=f.request_handle LEFT JOIN ai_device d ON d.id=r.device_id WHERE f.id=? AND f.browser_hash=?", id, browser.get("id_hash"));
        if (rows.isEmpty()) throw new CustomerFailure(404, "FLOW_NOT_FOUND");
        var flow = rows.getFirst();
        String authorization = "NONE";
        if (flow.get("subject_key") != null && flow.get("authorization_generation") != null) {
            var grants = db.queryForList("SELECT status FROM customer_authorization WHERE subject_key=?", flow.get("subject_key"));
            if (!grants.isEmpty()) authorization = grants.getFirst().get("status").toString();
        }
        String status=flow.get("status").toString();
        if ((status.equals("AWAITING_IDENTITY") || status.equals("EXCHANGING")) && number(flow,"expires_at_ms")<=clock.millis()) status="FLOW_EXPIRED";
        String requestStatus;
        try { request(flow.get("request_handle").toString()); requestStatus="AVAILABLE"; }
        catch (CustomerFailure unavailable) { requestStatus=unavailable.status==410 ? "EXPIRED" : "CONSUMED"; }
        return Map.of("flow_id", id, "status", status, "authorization_status", authorization,
                "device_label", flow.get("alias")==null ? "小智设备" : flow.get("alias"), "purpose",flow.get("purpose"),"request_status",requestStatus);
    }
    String callback(String cookie, String state, String code, String error) {
        var browser = browser(cookie);
        if (state == null || state.length() > 128) throw new CustomerFailure(400, "INVALID_STATE");
        var rows = db.queryForList("SELECT * FROM customer_identity_flow WHERE state_hash=? AND browser_hash=?", CustomerSecrets.hash(state), browser.get("id_hash"));
        if (rows.isEmpty()) throw new CustomerFailure(400, "INVALID_STATE");
        var flow = rows.getFirst();
        String id = flow.get("id").toString();
        if (number(flow, "expires_at_ms") <= clock.millis()) throw new CustomerFailure(410, "FLOW_EXPIRED");
        var request = request(flow.get("request_handle").toString());
        if (number(request, "permission_version") != number(flow, "permission_version") || !request.get("purpose").equals(flow.get("purpose")))
            throw new CustomerFailure(409, "STATE_CHANGED");
        // Commit the one-time claim BEFORE contacting the provider. Never replay after a crash/lost response.
        if (db.update("UPDATE customer_identity_flow SET status='EXCHANGING' WHERE id=? AND status='AWAITING_IDENTITY'", id) != 1)
            throw new CustomerFailure(409, "FLOW_ALREADY_USED");
        if (error != null || code == null || code.length() > 4096) {
            db.update("UPDATE customer_identity_flow SET status='IDENTITY_REFUSED',verifier='' WHERE id=?", id);
            return id;
        }
        try {
            var identity = market.exchange(code, config.decrypt(flow.get("verifier").toString()), flow.get("nonce").toString());
            transactions.executeWithoutResult(tx -> {
                db.queryForObject("SELECT id FROM customer_identity_barrier WHERE id=1 FOR UPDATE", Integer.class);
                if (number(flow,"expires_at_ms")<=clock.millis()) throw new CustomerFailure(410,"FLOW_EXPIRED");
                var currentRequest = request(flow.get("request_handle").toString());
                if (number(currentRequest, "permission_version") != number(flow, "permission_version")) throw new CustomerFailure(409, "STATE_CHANGED");
                String subjectKey = CustomerSecrets.hash(config.issuer + "\n" + identity.subject());
                long now = clock.millis();
                String generation = identity.businessKey()==null ? null : CustomerSecrets.random();
                if (generation!=null) {
                    voice.reconcileSubject(config.issuer,identity.subject());
                    String access = config.encrypt(identity.access()), refresh = config.encrypt(identity.refresh()), key = config.encrypt(identity.businessKey());
                    db.update("INSERT INTO customer_authorization(subject_key,generation,started_at_ms,issuer,subject,access_token,refresh_token,access_expires_at_ms,business_key,key_expires_at_ms,status,updated_at_ms,started_order) VALUES (?,?,?,?,?,?,?,?,?,?,'ACTIVE',?,?) ON DUPLICATE KEY UPDATE subject_key=VALUES(subject_key)",
                            subjectKey, generation, flow.get("created_at_ms"), config.issuer, identity.subject(), access, refresh, identity.accessExpiresAt(), key, identity.keyExpiresAt(), now,flow.get("creation_order"));
                    db.update("UPDATE customer_authorization SET generation=?,started_at_ms=?,access_token=?,refresh_token=?,access_expires_at_ms=?,business_key=?,key_expires_at_ms=?,status='ACTIVE',updated_at_ms=?,started_order=? WHERE subject_key=? AND started_order<=?",
                            generation, flow.get("created_at_ms"), access, refresh, identity.accessExpiresAt(), key, identity.keyExpiresAt(), now,flow.get("creation_order"), subjectKey, flow.get("creation_order"));
                }
                // A token can be minted after logout while an older code exchange is in flight.
                // The originating flow, not token mint time, determines whether that login was ended.
                boolean ended = !db.queryForList("SELECT jti_hash FROM customer_logout_receipt WHERE issuer=? AND issued_at_ms>=? AND received_at_ms>=? AND (sid=? OR (sid IS NULL AND subject=?)) LIMIT 1",
                        config.issuer, number(flow,"created_at_ms")/1000*1000, flow.get("created_at_ms"), identity.sid(), identity.subject()).isEmpty();
                db.update("UPDATE customer_identity_flow SET status=?,verifier='',subject_key=?,issuer=?,subject=?,sid=?,identity_issued_at_ms=?,authorization_generation=? WHERE id=? AND status='EXCHANGING'",
                        ended ? "SESSION_ENDED" : "IDENTITY_VERIFIED", subjectKey, config.issuer, identity.subject(), identity.sid(), identity.issuedAt(), generation, id);
                db.update("UPDATE customer_browser SET last_activity_ms=? WHERE id_hash=?", now, browser.get("id_hash"));
            });
        } catch (Exception e) {
            db.update("UPDATE customer_identity_flow SET status='IDENTITY_FAILED',verifier='' WHERE id=? AND status='EXCHANGING'", id);
        }
        return id;
    }
    void backchannelLogout(String token) {
        var event = market.validateLogout(token);
        transactions.executeWithoutResult(tx -> {
            db.queryForObject("SELECT id FROM customer_identity_barrier WHERE id=1 FOR UPDATE", Integer.class);
            int inserted = db.update("INSERT IGNORE INTO customer_logout_receipt(jti_hash,issuer,sid,subject,issued_at_ms,expires_at_ms,received_at_ms) VALUES (?,?,?,?,?,?,?)",
                    CustomerSecrets.hash(event.issuer() + "\n" + event.jti()), event.issuer(), event.sid(), event.subject(), event.issuedAt(), event.expiresAt(), clock.millis());
            if (inserted == 0) return;
            if (event.sid() != null) {
                db.update("UPDATE customer_identity_flow SET status='SESSION_ENDED' WHERE issuer=? AND sid=? AND created_at_ms<=? AND status='IDENTITY_VERIFIED'",
                        event.issuer(), event.sid(), Math.min(event.issuedAt()+999,clock.millis()));
            } else {
                db.update("UPDATE customer_identity_flow SET status='SESSION_ENDED' WHERE issuer=? AND subject=? AND created_at_ms<=? AND status='IDENTITY_VERIFIED'",
                        event.issuer(), event.subject(), Math.min(event.issuedAt()+999,clock.millis()));
            }
        });
    }
    static long number(Map<String, Object> row, String key) { return ((Number) row.get(key)).longValue(); }
}
