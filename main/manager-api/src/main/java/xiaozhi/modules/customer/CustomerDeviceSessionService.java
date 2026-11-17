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

/** Owner-only device-page actions. No market requests occur inside these transactions. */
@Service
final class CustomerDeviceSessionService {
    private final JdbcTemplate db;
    private final CustomerIdentityService identities;
    private final Clock clock;
    private final TransactionTemplate transactions;
    private final boolean enabled;
    private final CustomerDevicePermissionTransitions transitions;
    private record Context(Map<String,Object> browser, Map<String,Object> session, Map<String,Object> device) {}
    CustomerDeviceSessionService(JdbcTemplate db, CustomerIdentityService identities, PlatformTransactionManager manager,
            @Qualifier("customerTime") Clock clock, @Value("${device-control.enabled:false}") boolean enabled,
            CustomerDevicePermissionTransitions transitions) {
        this.db=db;this.identities=identities;this.clock=clock;this.enabled=enabled;
        this.transitions=transitions;
        transactions=new TransactionTemplate(manager);
        transactions.setIsolationLevel(TransactionDefinition.ISOLATION_READ_COMMITTED);
    }
    Map<String,Object> signOut(String cookie,String csrf,String origin,String sessionId,String key) {
        if(key==null || !key.matches("[A-Za-z0-9_.:-]{1,128}")) throw new CustomerFailure(400,"INVALID_REQUEST");
        return transactions.execute(tx -> {
            var context=context(cookie,sessionId);
            if(!CustomerOidcConfig.ORIGIN.equals(origin) || csrf==null || !csrf.equals(context.browser().get("csrf")))
                throw new CustomerFailure(403,"CSRF_REJECTED");
            var previous=db.queryForList("SELECT * FROM customer_device_operation WHERE device_session_id=? AND key_hash=?",sessionId,CustomerSecrets.hash(key));
            if(!previous.isEmpty()) { touch(context);return receipt(previous.getFirst()); }
            var device=context.device();
            if(number(context.session(),"permission_version")!=number(device,"permission_version")) throw new CustomerFailure(409,"STATE_CHANGED");
            transitions.disable(device,"user_signed_out");
            long permission=number(device,"permission_version"),revision=number(device,"revision");
            String id=CustomerSecrets.random();
            db.update("INSERT INTO customer_device_operation(id,device_session_id,key_hash,permission_version,revision,created_at_ms) VALUES (?,?,?,?,?,?)",id,sessionId,CustomerSecrets.hash(key),permission,revision,clock.millis());
            touch(context);
            return Map.of("operation_id",id,"permission_version",permission,"execution","stored");
        });
    }
    Map<String,Object> operation(String cookie,String id) {
        return transactions.execute(tx -> {
            var rows=db.queryForList("SELECT * FROM customer_device_operation WHERE id=?",id);
            if(rows.isEmpty()) throw new CustomerFailure(404,"REQUEST_NOT_FOUND");
            var operation=rows.getFirst();
            var context=context(cookie,operation.get("device_session_id").toString());
            String execution="stored";
            if(number(context.device(),"permission_version")!=number(operation,"permission_version")) execution="superseded";
            else if("user_signed_out".equals(context.device().get("reason")) && !db.queryForList(
                    "SELECT 1 FROM dc_state_ack_history WHERE device_id=? AND permission_version=? AND revision>=? AND audio_stopped=true",
                    context.device().get("device_id"),operation.get("permission_version"),operation.get("revision")).isEmpty()) execution="device_acknowledged";
            return Map.of("operation_id",id,"permission_version",operation.get("permission_version"),"execution",execution);
        });
    }
    Map<String,Object> page(String cookie,String id) {
        return transactions.execute(tx -> {
            var context=context(cookie,id);
            var operations=db.queryForList("SELECT id FROM customer_device_operation WHERE device_session_id=? ORDER BY created_at_ms DESC LIMIT 1",id);
            return Map.of("device_session_id",id,"csrf",context.browser().get("csrf"),
                    "can_sign_out",number(context.session(),"permission_version")==number(context.device(),"permission_version"),
                    "operation_id",operations.isEmpty()?"":operations.getFirst().get("id"));
        });
    }
    String sessionForFlow(String cookie,String flowId) {
        var browser=identities.browser(cookie);
        var rows=db.queryForList("SELECT id FROM customer_device_session WHERE flow_id=? AND browser_hash=?",flowId,browser.get("id_hash"));
        return rows.isEmpty()?null:rows.getFirst().get("id").toString();
    }
    private Context context(String cookie,String id) {
        if(!enabled) throw new CustomerFailure(503,"TEMPORARILY_UNAVAILABLE");
        db.queryForObject("SELECT id FROM customer_identity_barrier WHERE id=1 FOR UPDATE",Integer.class);
        var browser=identities.browser(cookie);
        db.queryForMap("SELECT * FROM customer_browser WHERE id_hash=? FOR UPDATE",browser.get("id_hash"));
        browser=identities.browser(cookie);
        var rows=db.queryForList("SELECT s.*,f.status AS identity_status,f.subject_key,r.control_credential_hash FROM customer_device_session s "
                +"JOIN customer_identity_flow f ON f.id=s.flow_id JOIN customer_device_request r ON r.handle=f.request_handle WHERE s.id=? AND s.browser_hash=?",id,browser.get("id_hash"));
        if(rows.isEmpty()) throw new CustomerFailure(404,"REQUEST_NOT_FOUND");
        var session=rows.getFirst();
        if(!"IDENTITY_VERIFIED".equals(session.get("identity_status"))) throw new CustomerFailure(401,"IDENTITY_REQUIRED");
        var device=db.queryForMap("SELECT * FROM dc_device WHERE device_id=? FOR UPDATE",session.get("device_id"));
        if(!Boolean.TRUE.equals(device.get("admitted")) || session.get("control_credential_hash")==null
                || !session.get("control_credential_hash").equals(device.get("credential_hash"))) throw new CustomerFailure(409,"STATE_CHANGED");
        if(device.get("owner_subject")==null || number(session,"ownership_version")!=number(device,"ownership_version")
                || !session.get("owner_subject_key").equals(session.get("subject_key"))
                || !session.get("owner_subject_key").equals(CustomerSecrets.hash(device.get("owner_issuer")+"\n"+device.get("owner_subject"))))
            throw new CustomerFailure(403,"OWNER_REQUIRED");
        return new Context(browser,session,device);
    }
    private void touch(Context context) { db.update("UPDATE customer_browser SET last_activity_ms=? WHERE id_hash=?",clock.millis(),context.browser().get("id_hash")); }
    private Map<String,Object> receipt(Map<String,Object> row) { return Map.of("operation_id",row.get("id"),"permission_version",row.get("permission_version"),"execution","stored"); }
    private static long number(Map<String,Object> row,String field) { return CustomerIdentityService.number(row,field); }
}
