package xiaozhi.modules.speaker;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import static xiaozhi.modules.speaker.SpeakerSupport.*;

/** One device-bound authorization transaction. Upstream I/O never holds a device row lock. */
@Service
public class SpeakerLogins {
    private final JdbcTemplate db;
    private final SpeakerDevices devices;
    private final SpeakerSettings settings;
    private final SpeakerMarket market;
    private final SpeakerCrypto crypto;
    private final SpeakerGrants grants;
    private final ObjectMapper json=new ObjectMapper();
    private static final Set<String> ACTIVE=Set.of("requesting","pending","polling","authorized","preparing");
    public SpeakerLogins(JdbcTemplate db,SpeakerDevices devices,SpeakerSettings settings,SpeakerMarket market,SpeakerCrypto crypto,SpeakerGrants grants) {
        this.db=db;this.devices=devices;this.settings=settings;this.market=market;this.crypto=crypto;this.grants=grants;
    }
    public Map<String,Object> begin(SpeakerDevices.Identity identity) {
        settings.requireEnabled();
        if(settings.clientId().isBlank()) throw new SpeakerFailure(503,"MARKET_CLIENT_NOT_CONFIGURED");
        // Fail before creating an upstream transaction when private persistence is not ready.
        crypto.encrypt("storage-readiness");
        String id=devices.transactions.execute(tx -> {
            var device=devices.load(identity,true);long now=System.currentTimeMillis();
            if(device.get("login_id")!=null) {
                var existing=db.queryForList("SELECT * FROM speaker_login WHERE id=?",device.get("login_id"));
                if(!existing.isEmpty() && ACTIVE.contains(existing.getFirst().get("status")) && number(existing.getFirst(),"expires_at_ms")>now)
                    return existing.getFirst().get("id").toString();
            }
            String next=UUID.randomUUID().toString();
            db.update("INSERT INTO speaker_login(id,device_id,credential_hash,binding_version,issuer,client_id,status,next_poll_at_ms,expires_at_ms,updated_at_ms) "
                    + "VALUES (?,?,?,?,?,?,'requesting',?,?,?)",next,identity.id(),identity.hash(),device.get("binding_version"),settings.issuer(),settings.clientId(),now,now+30_000,now);
            db.update("UPDATE speaker_device_state SET login_id=? WHERE device_id=?",next,identity.id());
            return next;
        });
        var row=db.queryForMap("SELECT * FROM speaker_login WHERE id=?",id);
        // Claim the requesting row; a second begin must not create a second provider transaction.
        if("requesting".equals(row.get("status")) && db.update("UPDATE speaker_login SET status='preparing' WHERE id=? AND status='requesting'",id)==1) {
            try {
                var request=market.begin();long now=System.currentTimeMillis();
                db.update("UPDATE speaker_login SET status='pending',device_code_cipher=?,verification_uri=?,user_code=?,interval_ms=?,next_poll_at_ms=?,"
                        + "expires_at_ms=?,updated_at_ms=? WHERE id=? AND status='preparing'",crypto.encrypt(request.deviceCode()),request.verificationUri(),request.userCode(),
                        request.interval()*1000,now+request.interval()*1000,now+request.expiresIn()*1000,now,id);
            } catch(SpeakerFailure failure) { fail(id,"failed",failure.getMessage()); }
        }
        return status(identity,id);
    }
    public Map<String,Object> status(SpeakerDevices.Identity identity,String id) {
        devices.load(identity,false);
        var rows=db.queryForList("SELECT * FROM speaker_login WHERE id=? AND device_id=? AND credential_hash=?",id,identity.id(),identity.hash());
        if(rows.isEmpty()) throw new SpeakerFailure(404,"LOGIN_NOT_FOUND");
        var row=rows.getFirst();
        var result=new LinkedHashMap<String,Object>();
        for(String field:new String[]{"id","status","verification_uri","user_code","expires_at_ms","error_code"}) result.put(field,row.get(field));
        result.put("server_time_ms",System.currentTimeMillis());
        return result;
    }
    @Scheduled(fixedDelayString="${speaker.login-poll-delay-ms:1000}")
    public void advanceDue() {
        if(!settings.enabled() || settings.clientId().isBlank()) return;
        long now=System.currentTimeMillis();
        db.update("UPDATE speaker_login SET status='expired',device_code_cipher=NULL,tokens_cipher=NULL,error_code='LOGIN_EXPIRED' "
                + "WHERE status IN ('requesting','preparing','pending','authorized') AND expires_at_ms<=?",now);
        db.update("UPDATE speaker_login SET status='failed',device_code_cipher=NULL,tokens_cipher=NULL,error_code='LOGIN_INTERRUPTED' "
                + "WHERE status='polling' AND updated_at_ms<?",now-30_000);
        for(var row:db.queryForList("SELECT * FROM speaker_login WHERE issuer=? AND client_id=? AND status IN ('pending','authorized') "
                + "AND next_poll_at_ms<=? AND expires_at_ms>? ORDER BY next_poll_at_ms LIMIT 5",settings.issuer(),settings.clientId(),now,now)) {
            String id=row.get("id").toString();String status=row.get("status").toString();
            if(db.update("UPDATE speaker_login SET status='polling',updated_at_ms=? WHERE id=? AND status=?",now,id,status)!=1) continue;
            try {
                if("pending".equals(status)) poll(row); else complete(row);
            } catch(Exception failure) {
                long delay=Math.min(60_000,Math.max(5_000,number(row,"interval_ms")*2));
                // Authorization codes are polled only before expiry; known successful token responses are retained for provisioning retry.
                db.update("UPDATE speaker_login SET status=?,interval_ms=?,next_poll_at_ms=?,updated_at_ms=?,error_code='MARKET_UNAVAILABLE' "
                        + "WHERE id=? AND status='polling'",status,delay,System.currentTimeMillis()+delay,System.currentTimeMillis(),id);
            }
        }
    }
    private void poll(Map<String,Object> row) throws Exception {
        String id=row.get("id").toString();
        var response=market.poll(crypto.decrypt(row.get("device_code_cipher").toString()));
        if(response.tokens()!=null) {
            String encrypted=crypto.encrypt(json.writeValueAsString(response.tokens()));long now=System.currentTimeMillis();
            db.update("UPDATE speaker_login SET status='authorized',device_code_cipher=NULL,tokens_cipher=?,expires_at_ms=?,next_poll_at_ms=?,updated_at_ms=?,error_code=NULL "
                    + "WHERE id=? AND status='polling'",encrypted,response.tokens().expiresAt(),now,now,id);
            return;
        }
        String error=response.error();
        if(Set.of("authorization_pending","slow_down").contains(error)) {
            long interval=number(row,"interval_ms")+("slow_down".equals(error)?5000:0);long now=System.currentTimeMillis();
            db.update("UPDATE speaker_login SET status='pending',interval_ms=?,next_poll_at_ms=?,updated_at_ms=?,error_code=NULL WHERE id=? AND status='polling'",
                    interval,now+interval,now,id);
        } else fail(id,"access_denied".equals(error)?"denied":"failed",error);
    }
    private void complete(Map<String,Object> row) throws Exception {
        String id=row.get("id").toString();
        var received=json.readValue(crypto.decrypt(row.get("tokens_cipher").toString()),SpeakerMarket.Tokens.class);
        if(received.expiresAt()<=System.currentTimeMillis()) { fail(id,"expired","LOGIN_EXPIRED");return; }
        // Persist identity tokens before obtaining a business Key. A Key outage must not discard successful authorization.
        String customer=row.get("customer_id")==null ? grants.saveIdentity(received,market.profile(received),id) : row.get("customer_id").toString();
        var tokens=grants.currentTokens(customer);
        grants.saveCredential(customer,tokens,market.credential(tokens));
        devices.transactions.executeWithoutResult(tx -> {
            var device=devices.load(new SpeakerDevices.Identity(row.get("device_id").toString(),row.get("credential_hash").toString()),true);
            if(!id.equals(device.get("login_id")) || number(row,"binding_version")!=number(device,"binding_version")) {
                fail(id,"cancelled","DEVICE_CHANGED");return;
            }
            if(device.get("customer_id")!=null && !customer.equals(device.get("customer_id"))) {
                fail(id,"denied","OWNER_REQUIRED");return;
            }
            if(!SpeakerGrants.usable(grants.grant(customer),System.currentTimeMillis())) throw new SpeakerFailure(503,"CREDENTIAL_PENDING");
            long now=System.currentTimeMillis();
            db.update("UPDATE speaker_device_state SET customer_id=?,use_enabled=TRUE,bound_at_ms=COALESCE(bound_at_ms,?),"
                    + "permission_version=permission_version+1,login_id=NULL WHERE device_id=?",customer,now,row.get("device_id"));
            db.update("UPDATE speaker_login SET status='completed',tokens_cipher=NULL,verification_uri=NULL,user_code=NULL,error_code=NULL,updated_at_ms=? "
                    + "WHERE id=? AND status='polling'",now,id);
            devices.event(row.get("device_id").toString(),device.get("customer_id")==null?"bound":"resumed","customer:"+customer);
        });
    }
    private void fail(String id,String state,String code) {
        db.update("UPDATE speaker_login SET status=?,error_code=?,device_code_cipher=NULL,tokens_cipher=NULL,verification_uri=NULL,user_code=NULL,updated_at_ms=? WHERE id=? AND status IN ('requesting','preparing','pending','polling','authorized')",
                state,code,System.currentTimeMillis(),id);
    }
}
