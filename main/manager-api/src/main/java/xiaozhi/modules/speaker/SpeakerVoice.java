package xiaozhi.modules.speaker;

import java.util.LinkedHashMap;
import java.util.Map;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import static xiaozhi.modules.speaker.SpeakerSupport.*;

/** Private voice adapter. Role revisions do not revoke an in-progress device conversation. */
@Service
public class SpeakerVoice {
    private final JdbcTemplate db;
    private final SpeakerSettings settings;
    private final SpeakerCrypto crypto;
    private final SpeakerGrants grants;
    private final SpeakerAgents agents;
    public SpeakerVoice(JdbcTemplate db,SpeakerSettings settings,SpeakerCrypto crypto,SpeakerGrants grants,SpeakerAgents agents) {
        this.db=db;this.settings=settings;this.crypto=crypto;this.grants=grants;this.agents=agents;
    }
    public Map<String,Object> ticket(Map<String,Object> device) {
        agents.current();
        java.net.URI url;
        try { url=java.net.URI.create(settings.voiceUrl()); }
        catch(Exception invalid) { throw new SpeakerFailure(503,"VOICE_NOT_CONFIGURED"); }
        if(!"/xiaozhi/v1/".equals(url.getPath()) || url.getHost()==null || url.getUserInfo()!=null || url.getQuery()!=null || url.getFragment()!=null
                || !("wss".equals(url.getScheme()) || "ws".equals(url.getScheme()) && "127.0.0.1".equals(url.getHost())))
            throw new SpeakerFailure(503,"VOICE_NOT_CONFIGURED");
        long now=System.currentTimeMillis();String value;long expiry;
        var existing=db.queryForList("SELECT token_cipher,expires_at_ms FROM speaker_voice_ticket WHERE device_id=? AND credential_hash=? "
                + "AND binding_version=? AND permission_version=? AND expires_at_ms>? ORDER BY expires_at_ms DESC LIMIT 1",device.get("device_id"),
                device.get("credential_hash"),device.get("binding_version"),device.get("permission_version"),now+60000);
        if(existing.isEmpty()) {
            value=token();expiry=now+300000;
            db.update("INSERT INTO speaker_voice_ticket(token_hash,token_cipher,device_id,credential_hash,binding_version,permission_version,expires_at_ms) VALUES (?,?,?,?,?,?,?)",
                    hash(value),crypto.encrypt(value),device.get("device_id"),device.get("credential_hash"),device.get("binding_version"),device.get("permission_version"),expiry);
        } else {value=crypto.decrypt(existing.getFirst().get("token_cipher").toString());expiry=number(existing.getFirst(),"expires_at_ms");}
        return Map.of("url",settings.voiceUrl(),"token",value,"expires_at_ms",expiry,"protocol_version",1,"permission_version",device.get("permission_version"));
    }
    public Object action(String action,Map<String,String> body) {
        settings.requireEnabled();
        if("open".equals(action)) return open(body.get("device_id"),body.get("token"));
        if("key-rejected".equals(action)) return rejected(body.get("rejection_receipt"));
        String session=body.get("session");
        if(session==null || !session.matches("[A-Za-z0-9_-]{43}")) throw new SpeakerFailure(401,"VOICE_DENIED");
        if("close".equals(action)) {db.update("DELETE FROM speaker_voice_session WHERE session_hash=?",hash(session));return Map.of("closed",true);}
        var device=checked("speaker_voice_session","session_hash",hash(session));
        var grant=grants.grant(device.get("customer_id").toString());
        if("check".equals(action)) return scope(device,grant);
        if("asr-access".equals(action)) return Map.of("provider","market");
        if(!"model-access".equals(action)) throw new SpeakerFailure(400,"INVALID_VOICE_ACTION");
        var result=new LinkedHashMap<>(scope(device,grant));var agent=agents.current();
        result.put("model",agent.get("llm_model"));result.put("asr_model",agent.get("asr_model"));result.put("agent",agent);
        result.put("api_key",crypto.decrypt(grant.get("credential_cipher").toString()));
        result.put("base_url",grant.get("credential_base").toString().replaceAll("/$","")+"/v1");
        result.put("authorization_order",grant.get("token_version"));
        result.put("rejection_receipt",crypto.encrypt(grant.get("customer_id")+"\n"+grant.get("generation")));
        return result;
    }
    private Object open(String id,String token) {
        if(id==null || token==null || !token.matches("[A-Za-z0-9_-]{43}")) throw new SpeakerFailure(401,"VOICE_DENIED");
        var device=checked("speaker_voice_ticket","token_hash",hash(token));
        if(!id.equals(device.get("device_id"))) throw new SpeakerFailure(401,"VOICE_DENIED");
        agents.current();String session=token();
        db.update("INSERT INTO speaker_voice_session(session_hash,device_id,credential_hash,binding_version,permission_version,expires_at_ms) VALUES (?,?,?,?,?,?)",
                hash(session),id,device.get("credential_hash"),device.get("binding_version"),device.get("permission_version"),System.currentTimeMillis()+86400000);
        var result=new LinkedHashMap<>(scope(device,grants.grant(device.get("customer_id").toString())));result.put("session",session);return result;
    }
    private Map<String,Object> checked(String table,String key,String value) {
        // Table/key are private constants; authorization is one consistent read, with no global device lock.
        var rows=db.queryForList("SELECT v.*,s.customer_id,s.use_enabled,s.admin_enabled,s.binding_version AS current_binding,"
                + "s.permission_version AS current_permission,d.credential_hash AS current_credential,d.admitted FROM "+table+" v "
                + "JOIN speaker_device_state s ON s.device_id=v.device_id JOIN dc_device d ON d.device_id=s.device_id WHERE v."+key+"=?",value);
        if(rows.isEmpty()) throw new SpeakerFailure(401,"VOICE_DENIED");
        var row=rows.getFirst();
        if(number(row,"expires_at_ms")<=System.currentTimeMillis() || !yes(row.get("admitted")) || !yes(row.get("admin_enabled")) || !yes(row.get("use_enabled"))
                || row.get("customer_id")==null || !row.get("credential_hash").equals(row.get("current_credential"))
                || number(row,"binding_version")!=number(row,"current_binding") || number(row,"permission_version")!=number(row,"current_permission")
                || !SpeakerGrants.usable(grants.grant(row.get("customer_id").toString()),System.currentTimeMillis()))
            throw new SpeakerFailure(403,"VOICE_DENIED");
        return row;
    }
    private Map<String,Object> scope(Map<String,Object> device,Map<String,Object> grant) {
        return Map.of("allowed",true,"permission_version",device.get("permission_version"),"authorization_scope",grant.get("generation"),
                "authorization_subject",hash(settings.issuer()+"\n"+settings.clientId()+"\n"+device.get("customer_id")),
                "permission_authorization_order",grant.get("token_version"));
    }
    private Object rejected(String receipt) {
        if(receipt==null || receipt.length()>1024) throw new SpeakerFailure(400,"INVALID_RECEIPT");
        String[] fields=crypto.decrypt(receipt).split("\n",-1);
        if(fields.length!=2) throw new SpeakerFailure(400,"INVALID_RECEIPT");
        db.update("UPDATE speaker_grant SET credential_status='rejected' WHERE customer_id=? AND client_id=? AND generation=?",
                fields[0],settings.clientId(),fields[1]);
        return Map.of("recorded",true);
    }
    @Scheduled(fixedDelay=3600000)
    public void cleanup() {
        if(!settings.enabled()) return;
        long now=System.currentTimeMillis();
        db.update("DELETE FROM speaker_voice_ticket WHERE expires_at_ms<?",now);
        db.update("DELETE FROM speaker_voice_session WHERE expires_at_ms<?",now);
    }
}
