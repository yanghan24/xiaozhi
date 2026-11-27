package xiaozhi.modules.speaker;

import java.net.URI;
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
    static final String SCAN_PATH = "/login/scan/";
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
        return begin(identity,false,false,null);
    }

    Map<String,Object> begin(SpeakerDevices.Identity identity, boolean resume) {
        return begin(identity,false,resume,null);
    }

    Map<String,Object> beginRecharge(SpeakerDevices.Identity identity) {
        return begin(identity,true,false,null);
    }

    Map<String,Object> refresh(SpeakerDevices.Identity identity, String previous) {
        if (!validTransaction(previous)) throw new SpeakerFailure(404,"LOGIN_NOT_FOUND");
        return begin(identity,false,true,previous);
    }

    private Map<String,Object> begin(SpeakerDevices.Identity identity, boolean rechargeConsent, boolean resume, String previous) {
        settings.requireEnabled();
        if(settings.clientId().isBlank()) throw new SpeakerFailure(503,"MARKET_CLIENT_NOT_CONFIGURED");
        // Fail before creating an upstream transaction when private persistence is not ready.
        crypto.encrypt("storage-readiness");
        String id=devices.transactions.execute(tx -> {
            var device=devices.loadForLogin(identity,true);long now=System.currentTimeMillis();
            if(rechargeConsent && !boundAndEnabled(device)) throw new SpeakerFailure(403,"DEVICE_RECHARGE_UNAVAILABLE");
            if (device.get("login_id")!=null) {
                String current=device.get("login_id").toString();
                var rows=db.queryForList("SELECT * FROM speaker_login WHERE id=? FOR UPDATE",current);
                if (!rows.isEmpty()) {
                    var existing=rows.getFirst();
                    boolean active=ACTIVE.contains(existing.get("status")) && number(existing,"expires_at_ms")>now;
                    boolean sameFlow=rechargeConsent==yes(existing.get("recharge_consent"));
                    if (!sameFlow && active) throw new SpeakerFailure(409,"DEVICE_LOGIN_IN_PROGRESS");
                    if (sameFlow && (active || resume)) {
                        if (!current.equals(previous) || active && completing(existing)) return current;
                        fail(current,"cancelled","LOGIN_REPLACED");
                    }
                }
            } else if (previous!=null) {
                var completed=db.queryForList("SELECT id FROM speaker_login WHERE id=? AND device_id=? AND credential_hash=? "
                        + "AND status='completed' AND recharge_consent=FALSE",previous,identity.id(),identity.hash());
                if (!completed.isEmpty()) return previous;
                throw new SpeakerFailure(409,"DEVICE_CHANGED");
            }
            String next=UUID.randomUUID().toString();
            db.update("INSERT INTO speaker_login(id,device_id,credential_hash,binding_version,issuer,client_id,status,next_poll_at_ms,expires_at_ms,updated_at_ms,recharge_consent) "
                    + "VALUES (?,?,?,?,?,?,'requesting',?,?,?,?)",next,identity.id(),identity.hash(),device.get("binding_version"),settings.issuer(),settings.clientId(),now,now+30_000,now,rechargeConsent);
            db.update("UPDATE speaker_device_state SET login_id=? WHERE device_id=?",next,identity.id());
            return next;
        });
        var row=db.queryForMap("SELECT * FROM speaker_login WHERE id=?",id);
        // Claim the requesting row; a second begin must not create a second provider transaction.
        if("requesting".equals(row.get("status")) && db.update("UPDATE speaker_login SET status='preparing' WHERE id=? AND status='requesting'",id)==1) {
            try {
                var request=rechargeConsent?market.beginRecharge():market.begin();long now=System.currentTimeMillis();
                db.update("UPDATE speaker_login SET status='pending',device_code_cipher=?,verification_uri=?,user_code=?,interval_ms=?,next_poll_at_ms=?,"
                        + "expires_at_ms=?,updated_at_ms=? WHERE id=? AND status='preparing'",crypto.encrypt(request.deviceCode()),request.verificationUri(),request.userCode(),
                        request.interval()*1000,now+request.interval()*1000,now+request.expiresIn()*1000,now,id);
            } catch(SpeakerFailure failure) { fail(id,"failed",failure.getMessage()); }
        }
        return status(identity,id);
    }
    public Map<String,Object> status(SpeakerDevices.Identity identity,String id) {
        devices.loadForLogin(identity,false);
        var rows=db.queryForList("SELECT * FROM speaker_login WHERE id=? AND device_id=? AND credential_hash=?",id,identity.id(),identity.hash());
        if(rows.isEmpty()) throw new SpeakerFailure(404,"LOGIN_NOT_FOUND");
        var row=rows.getFirst();
        long now=System.currentTimeMillis();
        var state=row.get("status");
        boolean waitingForConsent=waitingForConsent(row);
        boolean expired=waitingForConsent && number(row,"expires_at_ms")<=now;
        boolean showCode=waitingForConsent && !expired;
        boolean scanned=row.get("scanned_at_ms")!=null;
        var result=new LinkedHashMap<String,Object>();
        result.put("id",row.get("id"));
        result.put("status",expired?"expired":state);
        result.put("verification_uri",showCode?row.get("verification_uri"):null);
        result.put("user_code",showCode?row.get("user_code"):null);
        if (!yes(row.get("recharge_consent"))) {
            result.put("scanned",showCode && scanned);
            result.put("scan_path",showCode && !scanned?SCAN_PATH+id:null);
        }
        result.put("expires_at_ms",row.get("expires_at_ms"));
        result.put("error_code",expired?"LOGIN_EXPIRED":row.get("error_code"));
        result.put("server_time_ms",now);
        return result;
    }

    URI openScan(String id) {
        settings.requireEnabled();
        if (!validTransaction(id)) return null;
        var candidates=db.queryForList("SELECT device_id,credential_hash FROM speaker_login WHERE id=? AND recharge_consent=FALSE",id);
        if (candidates.isEmpty()) return null;
        var candidate=candidates.getFirst();
        var identity=new SpeakerDevices.Identity(candidate.get("device_id").toString(),candidate.get("credential_hash").toString());
        return devices.transactions.execute(tx -> {
            Map<String,Object> device;
            try {
                device=devices.loadForLogin(identity,true);
            } catch(SpeakerFailure failure) {
                if (failure.status==401 || failure.status==403) return null;
                throw failure;
            }
            if (!id.equals(device.get("login_id")) || !yes(device.get("admin_enabled"))) return null;
            var login=db.queryForMap("SELECT * FROM speaker_login WHERE id=? FOR UPDATE",id);
            long now=System.currentTimeMillis();
            if (!waitingForConsent(login) || login.get("scanned_at_ms")!=null || login.get("verification_uri")==null
                    || number(login,"expires_at_ms")<=now || number(login,"binding_version")!=number(device,"binding_version")) return null;
            var destination=market.verificationUri(login.get("verification_uri").toString());
            db.update("UPDATE speaker_login SET scanned_at_ms=? WHERE id=?",now,id);
            return destination;
        });
    }

    private static boolean validTransaction(String id) {
        return id != null && id.matches("[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}");
    }

    private static boolean waitingForConsent(Map<String,Object> row) {
        var state=row.get("status");
        return ("pending".equals(state) || "polling".equals(state)) && row.get("device_code_cipher")!=null;
    }

    private static boolean completing(Map<String,Object> row) {
        var state=row.get("status");
        return "authorized".equals(state) || "polling".equals(state) && !waitingForConsent(row);
    }
    @Scheduled(fixedDelayString="${speaker.login-poll-delay-ms:1000}")
    public void advanceDue() {
        if(!settings.enabled() || settings.clientId().isBlank()) return;
        long now=System.currentTimeMillis();
        db.update("UPDATE speaker_login SET status='expired',device_code_cipher=NULL,tokens_cipher=NULL,verification_uri=NULL,user_code=NULL,error_code='LOGIN_EXPIRED' "
                + "WHERE status IN ('requesting','preparing','pending','authorized') AND expires_at_ms<=?",now);
        db.update("UPDATE speaker_login SET status='failed',device_code_cipher=NULL,tokens_cipher=NULL,verification_uri=NULL,user_code=NULL,error_code='LOGIN_INTERRUPTED' "
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
            db.update("UPDATE speaker_login SET status='authorized',device_code_cipher=NULL,verification_uri=NULL,user_code=NULL,tokens_cipher=?,expires_at_ms=?,next_poll_at_ms=?,updated_at_ms=?,error_code=NULL "
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
        boolean rechargeConsent=yes(row.get("recharge_consent"));
        if (rechargeConsent && !received.allowsRecharge()) { fail(id,"denied","RECHARGE_SCOPE_REQUIRED");return; }
        // Profile I/O stays outside the lock. Recheck ownership before persisting
        // tokens: a stale authorization must not overwrite a later user's grant.
        var profile=row.get("customer_id")==null ? market.profile(received) : null;
        String customer=devices.transactions.execute(tx -> {
            var device=devices.loadForLogin(new SpeakerDevices.Identity(row.get("device_id").toString(),row.get("credential_hash").toString()),true);
            var current=db.queryForMap("SELECT * FROM speaker_login WHERE id=? FOR UPDATE",id);
            if(!"polling".equals(current.get("status")) || !id.equals(device.get("login_id"))
                    || number(row,"binding_version")!=number(device,"binding_version") || rechargeConsent && !boundAndEnabled(device)) {
                fail(id,"cancelled","DEVICE_CHANGED");return null;
            }
            if(device.get("customer_id")!=null) {
                var owner=db.queryForMap("SELECT issuer,subject FROM speaker_customer WHERE id=?",device.get("customer_id"));
                if(!settings.issuer().equals(owner.get("issuer")) || !received.subject().equals(owner.get("subject"))) {
                    fail(id,"denied","OWNER_REQUIRED");return null;
                }
            }
            return current.get("customer_id")==null ? grants.saveIdentity(received,profile,id) : current.get("customer_id").toString();
        });
        if(customer==null) return;
        devices.transactions.executeWithoutResult(tx -> {
            var device=devices.loadForLogin(new SpeakerDevices.Identity(row.get("device_id").toString(),row.get("credential_hash").toString()),true);
            if(!id.equals(device.get("login_id")) || number(row,"binding_version")!=number(device,"binding_version")
                    || rechargeConsent && !boundAndEnabled(device)) {
                fail(id,"cancelled","DEVICE_CHANGED");return;
            }
            if(device.get("customer_id")!=null && !customer.equals(device.get("customer_id"))) {
                fail(id,"denied","OWNER_REQUIRED");return;
            }
            if(!SpeakerGrants.usable(grants.grant(customer),System.currentTimeMillis())) throw new SpeakerFailure(503,"IDENTITY_PENDING");
            long now=System.currentTimeMillis();
            db.update("UPDATE speaker_device_state SET customer_id=?,use_enabled=TRUE,bound_at_ms=COALESCE(bound_at_ms,?),"
                    + "permission_version=permission_version+?,login_id=NULL,reauthorization_until_ms=0 WHERE device_id=?",
                    customer,now,rechargeConsent?0:1,row.get("device_id"));
            db.update("UPDATE speaker_login SET status='completed',tokens_cipher=NULL,verification_uri=NULL,user_code=NULL,error_code=NULL,updated_at_ms=? "
                    + "WHERE id=? AND status='polling'",now,id);
            devices.event(row.get("device_id").toString(),rechargeConsent?"recharge_authorized":device.get("customer_id")==null?"bound":"resumed","customer:"+customer);
        });
    }

    private static boolean boundAndEnabled(Map<String,Object> device) {
        return device.get("customer_id")!=null && yes(device.get("admin_enabled")) && yes(device.get("use_enabled"));
    }
    private void fail(String id,String state,String code) {
        db.update("UPDATE speaker_login SET status=?,error_code=?,device_code_cipher=NULL,tokens_cipher=NULL,verification_uri=NULL,user_code=NULL,updated_at_ms=? WHERE id=? AND status IN ('requesting','preparing','pending','polling','authorized')",
                state,code,System.currentTimeMillis(),id);
    }
}
