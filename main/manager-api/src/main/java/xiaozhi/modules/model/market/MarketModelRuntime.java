package xiaozhi.modules.model.market;

import java.util.*;
import java.time.Duration;
import java.util.concurrent.atomic.AtomicBoolean;
import cn.hutool.json.JSONObject;
import com.fasterxml.jackson.databind.*;
import jakarta.annotation.Resource;
import org.springframework.context.annotation.Lazy;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.scheduling.annotation.Async;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import xiaozhi.modules.config.service.ConfigService;
import xiaozhi.modules.speaker.SpeakerAgents;
import xiaozhi.modules.speaker.SpeakerFailure;
import xiaozhi.modules.speaker.SpeakerGrants;
import xiaozhi.modules.speaker.SpeakerSettings;

/** Resolve selected adapters into independent ORIGINAL provider configurations. */
@Service
public class MarketModelRuntime {
    private final MarketModelCatalog catalog;
    private final MarketResourceCredentials credentials;
    private final JdbcTemplate db;
    private final xiaozhi.modules.model.service.ModelConfigService models;
    @Resource @Lazy private SpeakerAgents agents;
    @Resource private ConfigService configurations;
    @Resource private SpeakerSettings speakerSettings;
    @Resource private SpeakerGrants grants;
    private final AtomicBoolean preparingCredentials = new AtomicBoolean();
    private final ObjectMapper json=new ObjectMapper();
    private static final List<String> KINDS=List.of("ASR","TTS","LLM","VLLM","Intent","Memory","SLM");
    private static final String REASONING_EFFORT="none";
    public MarketModelRuntime(MarketModelCatalog catalog,MarketResourceCredentials credentials,JdbcTemplate db,xiaozhi.modules.model.service.ModelConfigService models) {
        this.catalog=catalog;this.credentials=credentials;this.db=db;this.models=models;
    }
    public boolean hasMarket(Map<String,Object> config) {
        for(String kind:KINDS) if(config.get(kind) instanceof Map<?,?> values)
            for(Object value:values.values()) if(value instanceof Map<?,?> model && MarketModelBindings.isMarket(model)) return true;
        return false;
    }
    private record Selected(String kind,String id,Map<String,Object> config) {}
    @SuppressWarnings("unchecked")
    private List<Selected> entries(Map<String,Object> config) {
        var result=new ArrayList<Selected>();
        for(String kind:KINDS) if(config.get(kind) instanceof Map<?,?> values)
            for(var entry:values.entrySet()) if(entry.getValue() instanceof Map<?,?> model && MarketModelBindings.isMarket(model))
                result.add(new Selected(kind,entry.getKey().toString(),(Map<String,Object>)model));
        return result;
    }
    public void validate(Map<String,Object> config) {
        var selected=entries(config);if(selected.isEmpty()) return;
        var directory=catalog.list();
        var modules=(Map<?,?>)config.getOrDefault("selected_module",Map.of());
        boolean tools=false;
        if(config.get("Intent") instanceof Map<?,?> intents && intents.get(modules.get("Intent")) instanceof Map<?,?> intent)
            tools="function_call".equals(intent.get("type"));
        for(var model:selected) {
            requireEnabled(model.kind());
            MarketModelCompatibility.require(model.kind(),Objects.toString(model.config().get("model_name"),""),directory,
                    tools && model.kind().equals("LLM") && model.id().equals(modules.get("LLM")));
        }
    }
    @SuppressWarnings("unchecked")
    public Map<String,Object> configure(Map<String,Object> original,String customer) {
        Map<String,Object> config=json.convertValue(original,Map.class);
        if(!hasMarket(config)) return config;
        validate(config);
        var current=credentials.current(customer,null);
        var credential=current.credential();
        config.put("market_credential",current.forVoice());
        for(var selected:entries(config)) {
            var model=selected.config();
            String root=credential.baseUrl().replaceAll("/$","");
            model.put("api_key",credential.key());
            boolean nativeSpeech=selected.kind().equals("ASR") || selected.kind().equals("TTS");
            model.put("base_url",nativeSpeech ? root : root+"/v1");
            if(selected.kind().equals("Intent") || selected.kind().equals("Memory")) {
                var auxiliary=new LinkedHashMap<String,Object>();
                auxiliary.put("type","openai");auxiliary.put("source","aimarket");auxiliary.put("model_name",model.get("model_name"));
                auxiliary.put("base_url",root+"/v1");auxiliary.put("api_key",credential.key());
                if(selected.kind().equals("Intent")) auxiliary.put("response_format",Map.of("type","json_object"));
                auxiliary.put("thinking","default");
                auxiliary.put("reasoning_effort",REASONING_EFFORT);
                auxiliary.put("max_tokens",selected.kind().equals("Memory")?2000:512);
                ((Map<String,Object>)config.computeIfAbsent("LLM",k->new LinkedHashMap<>())).put(model.get("llm").toString(),auxiliary);
                model.remove("api_key");model.remove("base_url");
                if(selected.kind().equals("Intent")) model.putIfAbsent("functions",List.of());
            } else if(nativeSpeech) {
                model.put("output_dir","tmp/");
            } else {
                model.put("thinking","default");
                model.put("reasoning_effort",REASONING_EFFORT);
            }
        }
        return config;
    }

    @Async("taskExecutor")
    @Scheduled(fixedDelay=15_000)
    public void prepareActiveCredentials() {
        if(!speakerSettings.enabled() || speakerSettings.clientId().isBlank()
                || !preparingCredentials.compareAndSet(false,true)) return;
        try {
            var activeAgent=agents.defaultConfiguration().get("agent_id");
            if(activeAgent==null || !hasMarket(configurations.getAgentConfiguration(activeAgent.toString(),Map.of()))) return;
            long now=System.currentTimeMillis();
            for(String customer:grants.credentialsDue(now,now-MarketResourceCredentials.VERIFICATION_MS,
                    now-Duration.ofMinutes(5).toMillis())) {
                try {
                    credentials.forCustomer(customer);
                } catch(SpeakerFailure unavailable) {
                    // Account-specific errors are reflected in the credential state and retried later.
                }
            }
        } finally {
            preparingCredentials.set(false);
        }
    }
    private void requireEnabled(String kind) {
        var configured=models.getModelByIdFromCache(MarketModelBindings.id(kind));
        if(configured==null || !Integer.valueOf(1).equals(configured.getIsEnabled()))
            throw new SpeakerFailure(409,"MARKET_MODEL_DISABLED");
    }
    /** Internal Java summary/title calls must carry an owned conversation, never an admin's shared Key. */
    public JSONObject forConversation(JSONObject publicConfig,String kind,String conversationId) {
        var config=new JSONObject(publicConfig.toString());
        if(!MarketModelBindings.isMarket(config)) return config;
        requireEnabled(kind);
        if(conversationId==null) throw new SpeakerFailure(401,"MARKET_USER_REQUIRED");
        var rows=db.queryForList("SELECT c.customer_id FROM speaker_conversation c JOIN speaker_device_state s ON s.device_id=c.device_id "
                + "JOIN dc_device d ON d.device_id=s.device_id WHERE c.id=? AND c.customer_id=s.customer_id AND s.use_enabled=TRUE "
                + "AND s.admin_enabled=TRUE AND d.admitted=TRUE",conversationId);
        if(rows.size()!=1) throw new SpeakerFailure(403,"MARKET_USER_REQUIRED");
        MarketModelCompatibility.require(kind,config.getStr("model_name",""),catalog.list(),false);
        var credential=credentials.forCustomer(rows.getFirst().get("customer_id").toString());
        config.set("base_url",credential.baseUrl().replaceAll("/$","")+"/v1");config.set("api_key",credential.key());
        config.set("reasoning_effort",REASONING_EFFORT);
        return config;
    }
}
