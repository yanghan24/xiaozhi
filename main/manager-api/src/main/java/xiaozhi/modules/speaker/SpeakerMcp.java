package xiaozhi.modules.speaker;

import java.net.URI;
import java.util.*;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

/** Administrator catalogue only; discovery and execution use the original MCP client. */
@Service
public class SpeakerMcp {
    private final JdbcTemplate db;
    private final SpeakerCrypto crypto;
    private final ObjectMapper json=new ObjectMapper();
    public SpeakerMcp(JdbcTemplate db,SpeakerCrypto crypto) { this.db=db;this.crypto=crypto; }
    public record Configuration(String name,String url,String transport,Map<String,String> headers,
                                boolean clear_credentials,boolean enabled,long version) {}
    public Object list() {
        return db.queryForList("SELECT id,name,url,transport,enabled,version,updated_at_ms,"
                + "(headers_cipher IS NOT NULL) AS has_credentials FROM speaker_mcp ORDER BY updated_at_ms DESC,id");
    }
    @Transactional
    public Object save(String id,Configuration c) {
        if(c==null || c.name()==null || c.name().isBlank() || c.name().length()>128 || c.url()==null || c.url().length()>2048
                || !Set.of("sse","streamable-http").contains(c.transport()==null?"":c.transport()))
            throw new SpeakerFailure(400,"INVALID_MCP");
        try {
            URI uri=URI.create(c.url());
            if(!Set.of("https","http").contains(uri.getScheme()) || uri.getHost()==null || uri.getUserInfo()!=null
                    || uri.getQuery()!=null || uri.getFragment()!=null) throw new IllegalArgumentException();
        } catch(Exception invalid) {throw new SpeakerFailure(400,"INVALID_MCP");}
        String credentials=null;
        if(c.headers()!=null && !c.headers().isEmpty()) {
            if(c.headers().size()>16 || c.headers().entrySet().stream().anyMatch(e->e.getKey()==null
                    || !e.getKey().matches("[A-Za-z0-9_-]{1,64}") || e.getValue()==null || e.getValue().length()>4096
                    || e.getValue().contains("\r") || e.getValue().contains("\n"))) throw new SpeakerFailure(400,"INVALID_MCP");
            try { credentials=crypto.encryptConfiguration(json.writeValueAsString(c.headers())); }
            catch(Exception invalid) {throw new SpeakerFailure(400,"INVALID_MCP");}
        }
        long now=System.currentTimeMillis();
        if(id==null) {
            id=UUID.randomUUID().toString();
            db.update("INSERT INTO speaker_mcp(id,name,url,transport,headers_cipher,enabled,updated_at_ms) VALUES (?,?,?,?,?,?,?)",
                    id,c.name().strip(),c.url(),c.transport(),credentials,c.enabled(),now);
        } else {
            if(db.update("UPDATE speaker_mcp SET name=?,url=?,transport=?,headers_cipher=CASE WHEN ? THEN ? ELSE headers_cipher END,"
                    + "enabled=?,version=version+1,updated_at_ms=? WHERE id=? AND version=?",c.name().strip(),c.url(),c.transport(),
                    c.clear_credentials() || credentials!=null,credentials,c.enabled(),now,id,c.version())!=1)
                throw new SpeakerFailure(409,"MCP_CHANGED");
            changed(id);
        }
        return Map.of("id",id);
    }
    public Object selected(String agent) {
        return db.queryForList("SELECT service_id FROM speaker_agent_mcp WHERE agent_id=? ORDER BY service_id",agent)
                .stream().map(row->row.get("service_id")).toList();
    }
    public void removeAgent(String agent) { db.update("DELETE FROM speaker_agent_mcp WHERE agent_id=?",agent); }
    @Transactional
    public Object select(String agent,List<String> services) {
        if(services==null || services.size()>16 || new HashSet<>(services).size()!=services.size())
            throw new SpeakerFailure(400,"INVALID_MCP");
        if(db.queryForList("SELECT id FROM ai_agent WHERE id=? FOR UPDATE",agent).isEmpty())
            throw new SpeakerFailure(404,"AGENT_NOT_FOUND");
        for(String id:services) if(db.queryForList("SELECT id FROM speaker_mcp WHERE id=? FOR UPDATE",id).isEmpty())
            throw new SpeakerFailure(404,"MCP_NOT_FOUND");
        db.update("DELETE FROM speaker_agent_mcp WHERE agent_id=?",agent);
        for(String id:services) db.update("INSERT INTO speaker_agent_mcp(agent_id,service_id) VALUES (?,?)",agent,id);
        db.update("UPDATE speaker_runtime SET revision=revision+1 WHERE id=1 AND configured_agent_id=?",agent);
        return Map.of("stored",true);
    }
    public Map<String,Object> configuration(String agent) {
        var result=new LinkedHashMap<String,Object>();
        for(var row:db.queryForList("SELECT s.* FROM speaker_mcp s JOIN speaker_agent_mcp a ON a.service_id=s.id WHERE a.agent_id=? AND s.enabled=TRUE ORDER BY s.id",agent)) {
            var config=new LinkedHashMap<String,Object>();config.put("url",row.get("url"));config.put("transport",row.get("transport"));
            config.put("tool_prefix","mcp_"+row.get("id").toString().replace("-","").substring(0,12)+"_");
            if(row.get("headers_cipher")!=null) {
                try {config.put("headers",json.readValue(crypto.decryptConfiguration(row.get("headers_cipher").toString()),new TypeReference<Map<String,String>>(){}));}
                catch(Exception unavailable) {throw new SpeakerFailure(503,"MCP_CREDENTIALS_UNAVAILABLE");}
            }
            result.put(row.get("id").toString(),config);
        }
        return result;
    }
    private void changed(String id) {
        db.update("UPDATE speaker_runtime SET revision=revision+1 WHERE id=1 AND BINARY configured_agent_id IN "
                + "(SELECT BINARY agent_id FROM speaker_agent_mcp WHERE service_id=?)",id);
    }
}
