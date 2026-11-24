package xiaozhi.modules.speaker;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import static xiaozhi.modules.speaker.SpeakerSupport.*;

/** Local device actions never revoke a user's application-wide authorization. */
@Service
public class SpeakerOperations {
    private static final long REAUTHORIZATION_WINDOW_MS = 600_000;
    private final JdbcTemplate db;
    private final SpeakerDevices devices;
    private final SpeakerGrants grants;
    private final SpeakerVoice voice;
    public SpeakerOperations(JdbcTemplate db,SpeakerDevices devices,SpeakerGrants grants,SpeakerVoice voice) {
        this.db=db;this.devices=devices;this.grants=grants;this.voice=voice;
    }
    public Map<String,Object> state(SpeakerDevices.Identity identity) {
        return devices.transactions.execute(tx -> {
            var device=devices.load(identity,true);var result=new LinkedHashMap<String,Object>();
            result.put("protocol_version",2);result.put("device_id",identity.id());
            result.put("binding_version",device.get("binding_version"));result.put("permission_version",device.get("permission_version"));
            result.put("binding_state",device.get("customer_id")==null?"unbound":"bound");
            String status=useStatus(device);result.put("voice",null);
            if ("authorized".equals(status) && number(device,"reauthorization_until_ms")>System.currentTimeMillis()
                    && mayRequestReauthorization(device.get("customer_id").toString())) {
                status="authorization_required";
            }
            result.put("standby_weather_allowed","authorized".equals(status));
            result.put("account_balance_allowed","authorized".equals(status));
            if("authorized".equals(status)) {
                try { result.put("voice",voice.ticket(device)); status="ready"; }
                catch(SpeakerFailure failure) {
                    if(failure.status!=503) throw failure;
                    status=failure.getMessage().toLowerCase(java.util.Locale.ROOT);
                }
            }
            result.put("use_status",status);result.put("server_time_ms",System.currentTimeMillis());
            result.put("lease_expires_in_ms","ready".equals(status)?60000:0);
            result.put("login_id",device.get("login_id"));
            var owner=device.get("customer_id")==null?null:db.queryForMap("SELECT display_name FROM speaker_customer WHERE id=?",device.get("customer_id"));
            result.put("customer_name",owner==null?null:owner.get("display_name"));
            return result;
        });
    }

    public Map<String,Boolean> requestReauthorization(String customer,String id,long expected,long operator) {
        return devices.transactions.execute(tx -> {
            var rows=db.queryForList("""
                    SELECT s.* FROM speaker_device_state s JOIN dc_device d ON d.device_id=s.device_id
                    WHERE s.device_id=? AND s.customer_id=? AND d.admitted=TRUE FOR UPDATE
                    """,id,customer);
            if(rows.isEmpty()) throw new SpeakerFailure(404,"DEVICE_NOT_FOUND");
            var device=rows.getFirst();
            if(number(device,"permission_version")!=expected || !yes(device.get("admin_enabled"))
                    || !yes(device.get("use_enabled"))) throw new SpeakerFailure(409,"DEVICE_CHANGED");
            if(!mayRequestReauthorization(customer)) throw new SpeakerFailure(409,"MARKET_REAUTHORIZATION_NOT_NEEDED");
            db.update("UPDATE speaker_device_state SET reauthorization_until_ms=? WHERE device_id=?",
                    System.currentTimeMillis()+REAUTHORIZATION_WINDOW_MS,id);
            devices.event(id,"request_reauthorization","admin:"+operator);
            return Map.of("requested",true);
        });
    }

    private boolean mayRequestReauthorization(String customer) {
        var grant=grants.grant(customer);
        return grant!=null && "uncertain".equals(grant.get("token_status"));
    }
    public String useStatus(Map<String,Object> device) {
        if(!yes(device.get("admin_enabled"))) return "admin_disabled";
        if(device.get("customer_id")==null) return "unbound";
        if(!yes(device.get("use_enabled"))) return "signed_out";
        var grant=grants.grant(device.get("customer_id").toString());
        return SpeakerGrants.usable(grant,System.currentTimeMillis())?"authorized":"authorization_required";
    }

    public record AccountBalance(long permission_version, String balance) {}

    public AccountBalance accountBalance(SpeakerDevices.Identity identity) {
        var device = devices.load(identity, false);
        if (!"authorized".equals(useStatus(device))) {
            throw new SpeakerFailure(403, "DEVICE_BALANCE_UNAVAILABLE");
        }
        String customer = device.get("customer_id").toString();
        long permission = number(device, "permission_version");
        String balance = grants.deviceBalance(customer);
        // The Market request runs without a device lock. Recheck ownership before returning it.
        var current = devices.load(identity, false);
        if (!customer.equals(current.get("customer_id")) || number(current, "permission_version") != permission
                || !"authorized".equals(useStatus(current))) {
            throw new SpeakerFailure(409, "DEVICE_CHANGED");
        }
        return new AccountBalance(permission, balance);
    }
    public Map<String,Object> action(SpeakerDevices.Identity identity,String action,long expected,String requestKey,String actor) {
        if(!Set.of("sign-out","unbind").contains(action) || requestKey==null || !requestKey.matches("[A-Za-z0-9_.:-]{1,128}"))
            throw new SpeakerFailure(400,"INVALID_DEVICE_OPERATION");
        return devices.transactions.execute(tx -> {
            var device=devices.load(identity,true);String requestHash=hash(requestKey);
            var previous=db.queryForList("SELECT * FROM speaker_device_operation WHERE device_id=? AND request_hash=?",identity.id(),requestHash);
            if(!previous.isEmpty()) {
                var receipt=previous.getFirst();
                if(!identity.hash().equals(receipt.get("credential_hash")) || !action.equals(receipt.get("action")))
                    throw new SpeakerFailure(409,"OPERATION_CONFLICT");
                return Map.of("permission_version",receipt.get("permission_version"),"stored",true);
            }
            if(number(device,"permission_version")!=expected) throw new SpeakerFailure(409,"DEVICE_CHANGED");
            long next=Math.addExact(expected,1),binding=Math.addExact(number(device,"binding_version"),1);
            db.update("UPDATE speaker_login SET status='cancelled',device_code_cipher=NULL,tokens_cipher=NULL,verification_uri=NULL,user_code=NULL,error_code='DEVICE_CHANGED' "
                    + "WHERE device_id=? AND status IN ('requesting','preparing','pending','polling','authorized')",identity.id());
            db.update("UPDATE speaker_device_state SET use_enabled=FALSE,permission_version=?,binding_version=?,login_id=NULL,reauthorization_until_ms=0"
                    + ("unbind".equals(action)?",customer_id=NULL,bound_at_ms=NULL":"")+" WHERE device_id=?",next,binding,identity.id());
            db.update("INSERT INTO speaker_device_operation(device_id,request_hash,credential_hash,action,permission_version,created_at_ms) VALUES (?,?,?,?,?,?)",
                    identity.id(),requestHash,identity.hash(),action,next,System.currentTimeMillis());
            devices.event(identity.id(),action,actor);
            return Map.of("permission_version",next,"stored",true);
        });
    }
    public Map<String,Object> adminAction(String id,String action,long expected,String requestKey,long operator) {
        if(!Set.of("sign-out","unbind","enable","disable","rename").contains(action)) throw new SpeakerFailure(400,"INVALID_DEVICE_OPERATION");
        return devices.transactions.execute(tx -> {
            var rows=db.queryForList("SELECT d.credential_hash,s.* FROM dc_device d JOIN speaker_device_state s ON s.device_id=d.device_id WHERE d.device_id=? FOR UPDATE",id);
            if(rows.isEmpty()) throw new SpeakerFailure(404,"DEVICE_NOT_FOUND");
            var row=rows.getFirst();
            if(Set.of("sign-out","unbind").contains(action))
                return action(new SpeakerDevices.Identity(id,row.get("credential_hash").toString()),action,expected,requestKey,"admin:"+operator);
            if(number(row,"permission_version")!=expected) throw new SpeakerFailure(409,"DEVICE_CHANGED");
            if("rename".equals(action)) {
                if(requestKey==null || requestKey.isBlank() || requestKey.length()>128) throw new SpeakerFailure(400,"INVALID_DEVICE_NAME");
                db.update("UPDATE speaker_device_state SET name=? WHERE device_id=?",requestKey.strip(),id);
            } else db.update("UPDATE speaker_device_state SET admin_enabled=?,permission_version=permission_version+1,"
                    + "reauthorization_until_ms=0 WHERE device_id=?","enable".equals(action),id);
            devices.event(id,action,"admin:"+operator);
            return Map.of("stored",true);
        });
    }
}
