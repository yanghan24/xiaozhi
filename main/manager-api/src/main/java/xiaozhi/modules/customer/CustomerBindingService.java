package xiaozhi.modules.customer;

import java.time.Clock;
import java.util.Map;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.TransactionDefinition;
import org.springframework.transaction.support.TransactionTemplate;

/** Commits ownership, request consumption and its retry receipt as one operation. */
@Service
final class CustomerBindingService {
    private static final long MAX_VERSION=9007199254740991L;
    private final JdbcTemplate db;
    private final CustomerIdentityService identities;
    private final Clock clock;
    private final TransactionTemplate transactions;
    private final boolean enabled;
    private final CustomerVoicePermissions voice;

    CustomerBindingService(JdbcTemplate db, CustomerIdentityService identities,
            PlatformTransactionManager manager, @Qualifier("customerTime") Clock clock,
            @Value("${device-control.enabled:false}") boolean enabled, CustomerVoicePermissions voice) {
        this.db=db; this.identities=identities; this.clock=clock; this.enabled=enabled; this.voice=voice;
        transactions=new TransactionTemplate(manager);
        transactions.setIsolationLevel(TransactionDefinition.ISOLATION_READ_COMMITTED);
    }

    Map<String,Object> confirm(String cookie, String csrf, String origin, String flowId, String key) {
        return complete(cookie,csrf,origin,flowId,key,"bind");
    }
    Map<String,Object> openAccount(String cookie,String csrf,String origin,String flowId,String key) {
        return complete(cookie,csrf,origin,flowId,key,"account");
    }
    private Map<String,Object> complete(String cookie,String csrf,String origin,String flowId,String key,String purpose) {
        if (!enabled) throw new CustomerFailure(503,"TEMPORARILY_UNAVAILABLE");
        if (key==null || !key.matches("[A-Za-z0-9_.:-]{1,128}")) throw new CustomerFailure(400,"INVALID_REQUEST");
        return transactions.execute(tx -> {
            // Same short barrier as verified logout: neither can resurrect the other's old identity.
            db.queryForObject("SELECT id FROM customer_identity_barrier WHERE id=1 FOR UPDATE",Integer.class);
            var browser=identities.browser(cookie);
            db.queryForMap("SELECT * FROM customer_browser WHERE id_hash=? FOR UPDATE",browser.get("id_hash"));
            browser=identities.browser(cookie);
            if (!CustomerOidcConfig.ORIGIN.equals(origin) || csrf==null || !csrf.equals(browser.get("csrf")))
                throw new CustomerFailure(403,"CSRF_REJECTED");
            var flows=db.queryForList("SELECT * FROM customer_identity_flow WHERE id=? AND browser_hash=? FOR UPDATE",flowId,browser.get("id_hash"));
            if (flows.isEmpty()) throw new CustomerFailure(404,"FLOW_NOT_FOUND");
            var flow=flows.getFirst();
            if (!"IDENTITY_VERIFIED".equals(flow.get("status")) || flow.get("subject_key")==null)
                throw new CustomerFailure(401,"IDENTITY_REQUIRED");
            String action=flow.get("purpose").toString();
            if (!purpose.equals(action) && !("bind".equals(purpose) && "resume".equals(action) && voice.enabled()))
                throw new CustomerFailure(409,"PURPOSE_MISMATCH");

            var requests=db.queryForList("SELECT r.*,c.device_id AS control_device_id FROM customer_device_request r "
                    + "JOIN dc_request c ON c.request_id=r.control_request_id WHERE r.handle=?",flow.get("request_handle"));
            if (requests.isEmpty()) throw new CustomerFailure(404,"REQUEST_NOT_FOUND");
            var request=requests.getFirst();
            var device=db.queryForMap("SELECT * FROM dc_device WHERE device_id=? FOR UPDATE",request.get("control_device_id"));
            if (!Boolean.TRUE.equals(device.get("admitted")) || request.get("control_credential_hash")==null
                    || !request.get("control_credential_hash").equals(device.get("credential_hash")))
                throw new CustomerFailure(409,"STATE_CHANGED");

            var previous=db.queryForList("SELECT s.* FROM customer_binding_operation o JOIN customer_device_session s ON s.id=o.device_session_id "
                    + "WHERE o.browser_hash=? AND o.flow_id=? AND o.key_hash=?",browser.get("id_hash"),flowId,CustomerSecrets.hash(key));
            if (!previous.isEmpty()) {
                var receipt=previous.getFirst();
                if (!sameOwner(receipt,device) || !flow.get("subject_key").equals(receipt.get("owner_subject_key")))
                    throw new CustomerFailure(409,"STATE_CHANGED");
                touch(browser);
                return response(receipt);
            }
            if ("bind".equals(action) && device.get("owner_subject")!=null) {
                boolean own=flow.get("issuer").equals(device.get("owner_issuer")) && flow.get("subject").equals(device.get("owner_subject"));
                throw new CustomerFailure(409,own ? "STATE_CHANGED" : "DEVICE_ALREADY_BOUND");
            }
            if (!"bind".equals(action) && (!flow.get("issuer").equals(device.get("owner_issuer")) || !flow.get("subject").equals(device.get("owner_subject"))))
                throw new CustomerFailure(403,"OWNER_REQUIRED");
            var current=identities.request(flow.get("request_handle").toString());
            if (!action.equals(current.get("purpose")) || number(flow,"permission_version")!=number(device,"permission_version"))
                throw new CustomerFailure(409,"STATE_CHANGED");
            long revision=next(device,"revision"), permission=!"account".equals(action)?next(device,"permission_version"):number(device,"permission_version"), ownership="bind".equals(action)?next(device,"ownership_version"):number(device,"ownership_version");
            String reason=!"account".equals(action)?bindingReason(flow):device.get("reason").toString();
            if("resume".equals(action) && !"ready".equals(reason)) throw new CustomerFailure(409,"STATE_CHANGED");
            String session=CustomerSecrets.random();
            long now=clock.millis();
            if (number(current,"expires_at_ms")<=now) throw new CustomerFailure(410,"REQUEST_EXPIRED");
            // Ownership and local permission commit together after explicit phone confirmation.
            db.update("UPDATE dc_device SET owner_issuer=?,owner_subject=?,ownership_version=?,reason=?,"
                    + "permission_version=?,revision=?,current_request_id=NULL WHERE device_id=?",
                    flow.get("issuer"),flow.get("subject"),ownership,reason,permission,revision,device.get("device_id"));
            if(!"account".equals(action)) db.update("UPDATE dc_device SET local_enabled=? WHERE device_id=?","ready".equals(reason),device.get("device_id"));
            db.update("UPDATE dc_request SET status='consumed' WHERE request_id=?",current.get("control_request_id"));
            db.update("UPDATE customer_device_request SET consumed=true WHERE handle=?",flow.get("request_handle"));
            db.update("INSERT INTO customer_device_session(id,browser_hash,flow_id,device_id,owner_subject_key,ownership_version,permission_version,revision,created_at_ms) VALUES (?,?,?,?,?,?,?,?,?)",
                    session,browser.get("id_hash"),flowId,device.get("device_id"),flow.get("subject_key"),ownership,permission,revision,now);
            db.update("INSERT INTO customer_binding_operation(browser_hash,flow_id,key_hash,device_session_id,created_at_ms) VALUES (?,?,?,?,?)",
                    browser.get("id_hash"),flowId,CustomerSecrets.hash(key),session,now);
            touch(browser);
            return Map.of("device_session_id",session,"revision",revision,"permission_version",permission);
        });
    }

    boolean isBound(String cookie, String flowId) {
        var browser=identities.browser(cookie);
        var rows=db.queryForList("SELECT s.*,d.owner_issuer,d.owner_subject,d.ownership_version AS current_ownership_version "
                + "FROM customer_device_session s JOIN dc_device d ON d.device_id=s.device_id WHERE s.flow_id=? AND s.browser_hash=?",
                flowId,browser.get("id_hash"));
        if (rows.isEmpty()) return false;
        var row=rows.getFirst();
        return row.get("owner_subject")!=null && number(row,"ownership_version")==number(row,"current_ownership_version")
                && row.get("owner_subject_key").equals(CustomerSecrets.hash(row.get("owner_issuer")+"\n"+row.get("owner_subject")));
    }
    boolean isProductFlow(String cookie, String flowId) {
        if (!enabled) return false;
        var browser=identities.browser(cookie);
        return !db.queryForList("SELECT 1 FROM customer_identity_flow f JOIN customer_device_request r ON r.handle=f.request_handle "
                + "WHERE f.id=? AND f.browser_hash=? AND r.control_request_id IS NOT NULL",flowId,browser.get("id_hash")).isEmpty();
    }
    private String bindingReason(Map<String,Object> flow) {
        if (flow.get("authorization_generation")==null) return "market_auth_required";
        var grants=db.queryForList("SELECT * FROM customer_authorization WHERE subject_key=? FOR UPDATE",flow.get("subject_key"));
        if (grants.isEmpty()) return "market_auth_required";
        var grant=grants.getFirst();
        if (!flow.get("authorization_generation").equals(grant.get("generation"))) return "market_auth_required";
        if ("REFRESH_UNCERTAIN".equals(grant.get("status"))) return "market_auth_pending";
        long now=clock.millis();
        return "ACTIVE".equals(grant.get("status")) && number(grant,"access_expires_at_ms")>now
                && (grant.get("key_expires_at_ms")==null || number(grant,"key_expires_at_ms")>now)
                ? (voice.enabled()?"ready":"device_disabled") : "market_auth_required";
    }
    private boolean sameOwner(Map<String,Object> receipt, Map<String,Object> device) {
        return device.get("owner_subject")!=null && number(receipt,"ownership_version")==number(device,"ownership_version")
                && receipt.get("owner_subject_key").equals(CustomerSecrets.hash(device.get("owner_issuer")+"\n"+device.get("owner_subject")));
    }
    private void touch(Map<String,Object> browser) {
        db.update("UPDATE customer_browser SET last_activity_ms=? WHERE id_hash=?",clock.millis(),browser.get("id_hash"));
    }
    private Map<String,Object> response(Map<String,Object> receipt) {
        return Map.of("device_session_id",receipt.get("id"),"revision",receipt.get("revision"),"permission_version",receipt.get("permission_version"));
    }
    private long next(Map<String,Object> row, String field) {
        long value=number(row,field);
        if (value>=MAX_VERSION) throw new CustomerFailure(503,"TEMPORARILY_UNAVAILABLE");
        return value+1;
    }
    private static long number(Map<String,Object> row,String field) { return CustomerIdentityService.number(row,field); }
}
