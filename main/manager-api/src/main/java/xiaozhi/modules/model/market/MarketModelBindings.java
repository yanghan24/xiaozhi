package xiaozhi.modules.model.market;

import java.util.*;
import cn.hutool.json.JSONObject;
import com.fasterxml.jackson.databind.JsonNode;
import org.springframework.stereotype.Service;
import xiaozhi.modules.model.entity.ModelConfigEntity;
import xiaozhi.modules.model.service.ModelConfigService;
import xiaozhi.modules.speaker.SpeakerFailure;

/** One original model configuration per purpose, not one row per remote model. */
@Service
public class MarketModelBindings {
    private final ModelConfigService models;
    private final MarketModelCatalog catalog;
    private final xiaozhi.common.redis.RedisUtils redis;
    private final org.springframework.jdbc.core.JdbcTemplate db;
    public MarketModelBindings(ModelConfigService models,MarketModelCatalog catalog,xiaozhi.common.redis.RedisUtils redis,org.springframework.jdbc.core.JdbcTemplate db) { this.models=models;this.catalog=catalog;this.redis=redis;this.db=db; }
    public static String id(String type) { return "MARKET_"+MarketModelCompatibility.type(type); }
    public static boolean isMarket(Map<?,?> config) { return config!=null && "aimarket".equals(config.get("source")); }
    public List<JsonNode> options(String type) {
        return catalog.list().stream().filter(m->MarketModelCompatibility.supports(type,m)).toList();
    }
    public Map<String,Object> binding(String type) {
        String kind=MarketModelCompatibility.type(type);var model=models.selectById(id(kind));
        return Map.of("id",id(kind),"modelType",kind,"publicId",model==null?"":model.getConfigJson().getStr("model_name",""),
                "enabled",model!=null && Integer.valueOf(1).equals(model.getIsEnabled()));
    }
    @org.springframework.transaction.annotation.Transactional
    public synchronized Map<String,Object> save(String type,String publicId,boolean enabled) {
        String kind=MarketModelCompatibility.type(type);
        String id=id(kind);var entity=models.selectById(id);
        if(!enabled && entity!=null && publicId.equals(entity.getConfigJson().getStr("model_name"))) {
            if(Integer.valueOf(1).equals(entity.getIsDefault())) throw new SpeakerFailure(409,"DEFAULT_MODEL_CANNOT_DISABLE");
            entity.setIsEnabled(0);models.updateById(entity);invalidate(id);return binding(kind);
        }
        var candidate=MarketModelCompatibility.require(kind,publicId,catalog.list(),false);
        if(entity==null) {
            entity=new ModelConfigEntity();entity.setId(id);entity.setModelType(kind);entity.setModelCode(id);
            entity.setModelName("AI 集市 · "+Map.of("LLM","大语言模型","VLLM","视觉模型","ASR","语音识别","Intent","独立意图","Memory","摘要记忆").get(kind));entity.setIsDefault(0);entity.setSort(0);
        }
        var config=new JSONObject();
        config.set("source","aimarket");config.set("model_name",candidate.path("publicId").asText());
        config.set("protocol",candidate.path("protocol").asText());
        config.set("type",switch(kind) {case "ASR"->"aimarket_xfyun_asr_v2";case "Intent"->"intent_llm";case "Memory"->"mem_local_short";default->"openai";});
        if(kind.equals("Intent") || kind.equals("Memory")) config.set("llm","market_"+kind.toLowerCase(Locale.ROOT)+"_llm");
        entity.setConfigJson(config);entity.setIsEnabled(enabled?1:0);
        if(!enabled && Integer.valueOf(1).equals(entity.getIsDefault())) throw new SpeakerFailure(409,"DEFAULT_MODEL_CANNOT_DISABLE");
        if(models.selectById(id)==null) models.insert(entity);else models.updateById(entity);
        invalidate(id);
        return binding(kind);
    }
    private void invalidate(String id) {
        redis.delete(xiaozhi.common.redis.RedisKeys.getModelConfigById(id));
        redis.delete(xiaozhi.common.redis.RedisKeys.getModelNameById(id));
        redis.delete(xiaozhi.common.redis.RedisKeys.getServerConfigKey());
        db.update("UPDATE speaker_runtime r JOIN ai_agent a ON a.id=r.configured_agent_id SET r.revision=r.revision+1 WHERE r.id=1 "
                + "AND (? IN (a.llm_model_id,a.asr_model_id,a.vllm_model_id,a.slm_model_id,a.mem_model_id,a.intent_model_id) "
                + "OR EXISTS (SELECT 1 FROM ai_model_config c WHERE c.id IN (a.mem_model_id,a.intent_model_id) "
                + "AND JSON_UNQUOTE(JSON_EXTRACT(c.config_json,'$.llm'))=?))",id,id);
    }
}
