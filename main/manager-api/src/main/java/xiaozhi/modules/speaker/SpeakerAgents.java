package xiaozhi.modules.speaker;

import java.util.LinkedHashMap;
import java.util.Map;
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
    private final SpeakerAgentConfiguration fullConfiguration;
    public SpeakerAgents(JdbcTemplate db,SpeakerSettings settings,PlatformTransactionManager manager,SpeakerAgentConfiguration fullConfiguration) {
        this.db=db;this.settings=settings;transactions=new TransactionTemplate(manager);
        this.fullConfiguration=fullConfiguration;
    }
    public Map<String,Object> defaultConfiguration() {
        settings.requireEnabled();var runtime=db.queryForMap("SELECT * FROM speaker_runtime WHERE id=1");
        var result=new LinkedHashMap<String,Object>();result.put("agent_id",runtime.get("configured_agent_id"));result.put("revision",runtime.get("revision"));return result;
    }
    public String configurationId() {
        var id=defaultConfiguration().get("agent_id");
        if(id==null) throw new SpeakerFailure(503,"DEFAULT_AGENT_REQUIRED");
        return id.toString();
    }
    public String configurationRevision() {
        var rows=db.queryForList("SELECT a.id,a.updated_at,r.revision,COALESCE((SELECT MAX(version_no) FROM ai_agent_snapshot WHERE agent_id=a.id),0) AS version_no "
                + "FROM speaker_runtime r JOIN ai_agent a ON a.id=r.configured_agent_id WHERE r.id=1");
        if(rows.isEmpty()) throw new SpeakerFailure(503,"DEFAULT_AGENT_REQUIRED");
        var version=rows.getFirst();
        return version.get("id")+":"+version.get("revision")+":"+version.get("version_no")+":"+version.get("updated_at");
    }
    public Object selectConfiguration(String id,long revision) {
        settings.requireEnabled();
        return transactions.execute(tx -> {
            if(db.queryForList("SELECT id FROM ai_agent WHERE id=? FOR UPDATE",id).isEmpty()) throw new SpeakerFailure(404,"AGENT_NOT_FOUND");
            fullConfiguration.resolve(id,revision);
            if(db.update("UPDATE speaker_runtime SET configured_agent_id=?,default_agent_id=NULL,revision=revision+1 WHERE id=1 AND revision=?",id,revision)!=1)
                throw new SpeakerFailure(409,"AGENT_CHANGED");
            return Map.of("stored",true);
        });
    }
}
