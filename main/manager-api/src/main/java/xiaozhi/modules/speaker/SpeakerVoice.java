package xiaozhi.modules.speaker;

import java.util.LinkedHashMap;
import java.util.Map;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import static xiaozhi.modules.speaker.SpeakerSupport.*;

/** Connection identity only. Resource credentials and providers belong to upstream configuration. */
@Service
public class SpeakerVoice {
    private final JdbcTemplate db;
    private final SpeakerSettings settings;
    private final SpeakerCrypto crypto;
    private final SpeakerGrants grants;
    private final SpeakerAgents agents;
    private final xiaozhi.modules.sys.service.VoiceServiceEndpoints endpoints;
    @jakarta.annotation.Resource private SpeakerConversations conversations;
    public SpeakerVoice(JdbcTemplate db,SpeakerSettings settings,SpeakerCrypto crypto,SpeakerGrants grants,SpeakerAgents agents,xiaozhi.modules.sys.service.VoiceServiceEndpoints endpoints) {
        this.db=db;this.settings=settings;this.crypto=crypto;this.grants=grants;this.agents=agents;this.endpoints=endpoints;
    }
    public Map<String,Object> ticket(Map<String,Object> device) {
        agents.configurationId();
        String voiceUrl=endpoints.deviceUrl();
        long now=System.currentTimeMillis();String value;long expiry;
        var existing=db.queryForList("SELECT token_cipher,expires_at_ms FROM speaker_voice_ticket WHERE device_id=? AND credential_hash=? "
                + "AND binding_version=? AND permission_version=? AND expires_at_ms>? ORDER BY expires_at_ms DESC LIMIT 1",device.get("device_id"),
                device.get("credential_hash"),device.get("binding_version"),device.get("permission_version"),now+60000);
        if(existing.isEmpty()) {
            value=token();expiry=now+300000;
            db.update("INSERT INTO speaker_voice_ticket(token_hash,token_cipher,device_id,credential_hash,binding_version,permission_version,expires_at_ms) VALUES (?,?,?,?,?,?,?)",
                    hash(value),crypto.encrypt(value),device.get("device_id"),device.get("credential_hash"),device.get("binding_version"),device.get("permission_version"),expiry);
        } else {value=crypto.decrypt(existing.getFirst().get("token_cipher").toString());expiry=number(existing.getFirst(),"expires_at_ms");}
        return Map.of("url",voiceUrl,"token",value,"expires_at_ms",expiry,"protocol_version",1,"permission_version",device.get("permission_version"));
    }
    public Object action(String action,Map<String,String> body) {
        settings.requireEnabled();
        if("open".equals(action)) return open(body.get("device_id"),body.get("token"));
        String session=body.get("session");
        if(session==null || !session.matches("[A-Za-z0-9_-]{43}")) throw new SpeakerFailure(401,"VOICE_DENIED");
        if("close".equals(action)) {db.update("DELETE FROM speaker_voice_session WHERE session_hash=?",hash(session));return Map.of("closed",true);}
        var device=checked("speaker_voice_session","session_hash",hash(session));
        if("check".equals(action)) return scope(device);
        if("conversation".equals(action)) {
            var current=scope(device);
            String revision=current.get("configuration_revision").toString();
            if(!revision.equals(body.get("configuration_revision"))) throw new SpeakerFailure(409,"AGENT_CHANGED");
            return Map.of("history_id",conversations.create(device,revision,current.get("memory_scope").toString()));
        }
        throw new SpeakerFailure(400,"INVALID_VOICE_ACTION");
    }
    private Object open(String id,String token) {
        if(id==null || token==null || !token.matches("[A-Za-z0-9_-]{43}")) throw new SpeakerFailure(401,"VOICE_DENIED");
        var device=checked("speaker_voice_ticket","token_hash",hash(token));
        if(!id.equals(device.get("device_id"))) throw new SpeakerFailure(401,"VOICE_DENIED");
        agents.configurationId();String session=token();
        db.update("INSERT INTO speaker_voice_session(session_hash,device_id,credential_hash,binding_version,permission_version,expires_at_ms) VALUES (?,?,?,?,?,?)",
                hash(session),id,device.get("credential_hash"),device.get("binding_version"),device.get("permission_version"),System.currentTimeMillis()+86400000);
        var result=new LinkedHashMap<>(scope(device));result.put("session",session);return result;
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
    private Map<String,Object> scope(Map<String,Object> device) {
        String revision=agents.configurationRevision();
        return Map.of("allowed",true,"device_id",device.get("device_id"),"agent_id",revision.split(":",2)[0],
                "configuration_revision",revision,"permission_version",device.get("permission_version"),
                "memory_scope",hash(device.get("customer_id")+":"+device.get("device_id")+":"+revision));
    }
    public String customerForSession(String id,String session) {
        if(session==null || !session.matches("[A-Za-z0-9_-]{43}")) throw new SpeakerFailure(401,"VOICE_DENIED");
        var device=checked("speaker_voice_session","session_hash",hash(session));
        if(!id.equals(device.get("device_id"))) throw new SpeakerFailure(403,"VOICE_DENIED");
        return device.get("customer_id").toString();
    }
    public void requireDevice(String id) {
        var rows=db.queryForList("SELECT s.*,d.admitted FROM speaker_device_state s JOIN dc_device d ON d.device_id=s.device_id WHERE s.device_id=?",id);
        if(rows.size()!=1 || !yes(rows.getFirst().get("admitted")) || !yes(rows.getFirst().get("admin_enabled"))
                || !yes(rows.getFirst().get("use_enabled")) || rows.getFirst().get("customer_id")==null
                || !SpeakerGrants.usable(grants.grant(rows.getFirst().get("customer_id").toString()),System.currentTimeMillis()))
            throw new SpeakerFailure(403,"VOICE_DENIED");
    }
    public String memoryScope(String id,String revision) {
        String customer=db.queryForObject("SELECT customer_id FROM speaker_device_state WHERE device_id=?",String.class,id);
        return hash(customer+":"+id+":"+revision);
    }
    @Scheduled(fixedDelay=3600000)
    public void cleanup() {
        if(!settings.enabled()) return;
        long now=System.currentTimeMillis();
        db.update("DELETE FROM speaker_voice_ticket WHERE expires_at_ms<?",now);
        db.update("DELETE FROM speaker_voice_session WHERE expires_at_ms<?",now);
    }
}
