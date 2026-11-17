package xiaozhi.modules.speaker;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.UUID;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;

/** Administrator-owned role configuration. No customer credentials live in a role. */
@Service
public class SpeakerAgents {
    private final JdbcTemplate db;
    private final SpeakerSettings settings;
    private final TransactionTemplate transactions;
    public record Configuration(String name,String prompt,String llm_model,String asr_model,String tts_voice,long version) {}
    public SpeakerAgents(JdbcTemplate db,SpeakerSettings settings,PlatformTransactionManager manager) {
        this.db=db;this.settings=settings;transactions=new TransactionTemplate(manager);
    }
    public Object list() {
        settings.requireEnabled();var runtime=db.queryForMap("SELECT * FROM speaker_runtime WHERE id=1");
        var result=new LinkedHashMap<String,Object>();result.put("default_agent_id",runtime.get("default_agent_id"));result.put("revision",runtime.get("revision"));
        result.put("items",db.queryForList("SELECT * FROM speaker_agent ORDER BY created_at_ms,id"));return result;
    }
    public Map<String,Object> current() {
        var rows=db.queryForList("SELECT a.*,r.revision FROM speaker_runtime r JOIN speaker_agent a ON a.id=r.default_agent_id WHERE r.id=1");
        if(rows.isEmpty()) throw new SpeakerFailure(503,"DEFAULT_AGENT_REQUIRED");
        var role=new LinkedHashMap<>(rows.getFirst());
        role.put("signature",role.get("id")+":"+role.get("version")+":"+role.get("revision"));return role;
    }
    public Object save(String id,Configuration config) {
        settings.requireEnabled();validate(config);
        return transactions.execute(tx -> {
            long now=System.currentTimeMillis();
            if(id==null) {
                String next=UUID.randomUUID().toString();
                db.update("INSERT INTO speaker_agent(id,name,prompt,llm_model,asr_model,tts_voice,created_at_ms,updated_at_ms) VALUES (?,?,?,?,?,?,?,?)",
                        next,config.name().strip(),config.prompt().strip(),config.llm_model(),config.asr_model(),config.tts_voice(),now,now);
                return Map.of("id",next);
            }
            if(db.update("UPDATE speaker_agent SET name=?,prompt=?,llm_model=?,asr_model=?,tts_voice=?,version=version+1,updated_at_ms=? WHERE id=? AND version=?",
                    config.name().strip(),config.prompt().strip(),config.llm_model(),config.asr_model(),config.tts_voice(),now,id,config.version())!=1)
                throw new SpeakerFailure(409,"AGENT_CHANGED");
            db.update("UPDATE speaker_runtime SET revision=revision+1 WHERE id=1 AND default_agent_id=?",id);
            return Map.of("id",id);
        });
    }
    public Object select(String id,long revision) {
        settings.requireEnabled();
        return transactions.execute(tx -> {
            if(db.queryForList("SELECT id FROM speaker_agent WHERE id=?",id).isEmpty()) throw new SpeakerFailure(404,"AGENT_NOT_FOUND");
            if(db.update("UPDATE speaker_runtime SET default_agent_id=?,revision=revision+1 WHERE id=1 AND revision=?",id,revision)!=1)
                throw new SpeakerFailure(409,"AGENT_CHANGED");
            return Map.of("stored",true);
        });
    }
    private static void validate(Configuration c) {
        if(c==null || c.name()==null || c.name().isBlank() || c.name().length()>128 || c.prompt()==null || c.prompt().isBlank() || c.prompt().length()>16000
                || !model(c.llm_model()) || !model(c.asr_model()) || c.tts_voice()==null || !c.tts_voice().matches("[A-Za-z0-9-]{1,128}"))
            throw new SpeakerFailure(400,"INVALID_AGENT");
    }
    private static boolean model(String value) { return value!=null && value.matches("[A-Za-z0-9][A-Za-z0-9_./:-]{0,127}"); }
}
