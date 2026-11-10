package xiaozhi.modules.customer;

import java.time.Clock;
import java.util.Map;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.TransactionDefinition;
import org.springframework.transaction.support.TransactionTemplate;

/** Shared authority for device leases and every voice connection/call/output. */
@Service
public final class CustomerVoicePermissions {
    private final JdbcTemplate db;
    private final Clock clock;
    private final CustomerOidcConfig secrets;
    private final TransactionTemplate transactions;
    private final boolean enabled;
    private final String origin;
    private final String modelBase;
    public CustomerVoicePermissions(JdbcTemplate db,@Qualifier("customerTime") Clock clock,CustomerOidcConfig secrets,
            PlatformTransactionManager manager,@Value("${device-control.enabled:false}") boolean control,
            @Value("${device-control.voice-enabled:false}") boolean voice,
            @Value("${device-control.public-origin:${PUBLIC_ORIGIN:}}") String origin,
            @Value("${device-control.model-base-url:https://aimarket.hncig.cn/v1/}") String modelBase) {
        this.db=db;this.clock=clock;this.secrets=secrets;this.enabled=control && voice;this.origin=origin;
        this.modelBase=modelBase;
        transactions=new TransactionTemplate(manager);transactions.setIsolationLevel(TransactionDefinition.ISOLATION_READ_COMMITTED);
    }
    public boolean enabled(){return enabled;}
    public void barrier(){if(enabled) db.queryForObject("SELECT id FROM customer_identity_barrier WHERE id=1 FOR UPDATE",Integer.class);}
    public void reconcile(Map<String,Object> device) {
        if(!Boolean.TRUE.equals(device.get("local_enabled"))) return;
        String reason="device_disabled";
        if(enabled) {
            var grants=db.queryForList("SELECT * FROM customer_authorization WHERE issuer=? AND subject=?",device.get("owner_issuer"),device.get("owner_subject"));
            reason=grants.isEmpty()?"market_auth_required":blockedReason(grants.getFirst());
        }
        if(reason==null) return;
        long permission=next(device,"permission_version"),revision=next(device,"revision");
        db.update("UPDATE dc_device SET local_enabled=false,reason=?,permission_version=?,revision=?,current_request_id=NULL WHERE device_id=?",reason,permission,revision,device.get("device_id"));
        db.update("UPDATE dc_request SET status='replaced' WHERE device_id=? AND status='active'",device.get("device_id"));
        device.put("local_enabled",false);device.put("reason",reason);device.put("permission_version",permission);device.put("revision",revision);device.put("current_request_id",null);
    }
    // Model requests use the independently valid business Key. Access-token refresh alone
    // does not revoke it; uncertain or rejected refresh outcomes do, and never auto-resume.
    private String blockedReason(Map<String,Object> grant) {
        if("REFRESH_UNCERTAIN".equals(grant.get("status"))) return "market_auth_pending";
        if(!java.util.Set.of("ACTIVE","REFRESHING").contains(grant.get("status"))) return "market_auth_required";
        if(grant.get("key_expires_at_ms")!=null && number(grant,"key_expires_at_ms")<=clock.millis()) return "market_auth_required";
        return null;
    }
    /** Called under the identity barrier before replacing a shared grant. */
    void reconcileSubject(String issuer,String subject) {
        if(!enabled) return;
        for(var device:db.queryForList("SELECT * FROM dc_device WHERE owner_issuer=? AND owner_subject=? AND local_enabled=true ORDER BY device_id FOR UPDATE",issuer,subject)) reconcile(device);
    }
    @Scheduled(fixedDelayString="${device-control.permission-sync-ms:200}")
    public void synchronize() {
        if(!enabled) return;
        try {transactions.executeWithoutResult(tx->{barrier();
            for(var device:db.queryForList("SELECT * FROM dc_device WHERE local_enabled=true ORDER BY device_id FOR UPDATE")) reconcile(device);
        });} catch(Exception unavailable) { /* Every live gate independently fails closed. No credentials in logs. */ }
    }
    public Map<String,Object> voice(Map<String,Object> device,long now) {
        if(!enabled || !Boolean.TRUE.equals(device.get("local_enabled")) || !"ready".equals(device.get("reason"))) return null;
        var rows=db.queryForList("SELECT * FROM dc_voice_token WHERE device_id=? AND credential_hash=? AND ownership_version=? AND permission_version=? AND expires_at_ms>? ORDER BY expires_at_ms DESC LIMIT 1",
                device.get("device_id"),device.get("credential_hash"),device.get("ownership_version"),device.get("permission_version"),now+60000);
        String token;long expires;
        if(rows.isEmpty()) {
            token=CustomerSecrets.random();expires=now+300000;
            db.update("INSERT INTO dc_voice_token(token_hash,token_cipher,device_id,credential_hash,ownership_version,permission_version,expires_at_ms) VALUES (?,?,?,?,?,?,?)",
                    CustomerSecrets.hash(token),secrets.encrypt(token),device.get("device_id"),device.get("credential_hash"),device.get("ownership_version"),device.get("permission_version"),expires);
        } else {token=secrets.decrypt(rows.getFirst().get("token_cipher").toString());expires=number(rows.getFirst(),"expires_at_ms");}
        return Map.of("url","wss"+origin.substring(5)+"/xiaozhi/v1/","token",token,"expires_at_ms",expires,"protocol_version",1,"permission_version",device.get("permission_version"));
    }
    Map<String,Object> open(String deviceId,String token) {
        return transactions.execute(tx->{barrier();
            var rows=db.queryForList("SELECT * FROM dc_voice_token WHERE token_hash=? AND device_id=?",CustomerSecrets.hash(token),deviceId);
            if(rows.isEmpty() || number(rows.getFirst(),"expires_at_ms")<=clock.millis()) throw new CustomerFailure(401,"VOICE_DENIED");
            var device=checked(rows.getFirst());String session=CustomerSecrets.random();
            db.update("INSERT INTO dc_voice_session(id_hash,token_hash,created_at_ms) VALUES (?,?,?)",CustomerSecrets.hash(session),rows.getFirst().get("token_hash"),clock.millis());
            return Map.of("session",session,"permission_version",device.get("permission_version"));
        });
    }
    Map<String,Object> check(String session) {
        return transactions.execute(tx->{barrier();var device=checked(sessionToken(session));return Map.of("allowed",true,"permission_version",device.get("permission_version"));});
    }
    Map<String,Object> modelAccess(String session) {
        return transactions.execute(tx->{barrier();var device=checked(sessionToken(session));
            var grant=db.queryForMap("SELECT * FROM customer_authorization WHERE issuer=? AND subject=?",device.get("owner_issuer"),device.get("owner_subject"));
            return Map.of("api_key",secrets.decrypt(grant.get("business_key").toString()),"base_url",modelBase,"model","qwen3.8-flash");
        });
    }
    void close(String session){db.update("DELETE FROM dc_voice_session WHERE id_hash=?",CustomerSecrets.hash(session));}
    private Map<String,Object> sessionToken(String session) {
        var rows=db.queryForList("SELECT t.* FROM dc_voice_session s JOIN dc_voice_token t ON t.token_hash=s.token_hash WHERE s.id_hash=?",CustomerSecrets.hash(session));
        if(rows.isEmpty()) throw new CustomerFailure(401,"VOICE_DENIED");return rows.getFirst();
    }
    private Map<String,Object> checked(Map<String,Object> token) {
        if(!enabled) throw new CustomerFailure(503,"VOICE_UNAVAILABLE");
        var device=db.queryForMap("SELECT * FROM dc_device WHERE device_id=? FOR UPDATE",token.get("device_id"));
        reconcile(device);
        if(!Boolean.TRUE.equals(device.get("admitted")) || device.get("owner_subject")==null
                || !token.get("credential_hash").equals(device.get("credential_hash"))
                || number(token,"ownership_version")!=number(device,"ownership_version")
                || number(token,"permission_version")!=number(device,"permission_version")
                || !Boolean.TRUE.equals(device.get("local_enabled")) || !"ready".equals(device.get("reason"))) throw new CustomerFailure(403,"VOICE_DENIED");
        return device;
    }
    private static long next(Map<String,Object> row,String field){long value=number(row,field);if(value>=9007199254740991L)throw new CustomerFailure(503,"TEMPORARILY_UNAVAILABLE");return value+1;}
    private static long number(Map<String,Object> row,String field){return CustomerIdentityService.number(row,field);}
}
