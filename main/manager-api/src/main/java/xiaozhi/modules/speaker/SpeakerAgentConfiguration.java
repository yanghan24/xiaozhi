package xiaozhi.modules.speaker;

import java.util.Map;
import org.springframework.stereotype.Service;
import xiaozhi.modules.config.service.ConfigService;
import xiaozhi.modules.model.service.ModelConfigService;

/** Validate original providers without reducing them to model-name strings. */
@Service
public class SpeakerAgentConfiguration {
    private final ConfigService configurations;
    private final ModelConfigService models;
    @jakarta.annotation.Resource private xiaozhi.modules.model.market.MarketModelRuntime marketModels;
    public SpeakerAgentConfiguration(ConfigService configurations,ModelConfigService models) {
        this.configurations=configurations;this.models=models;
    }
    public Map<String,Object> resolve(String id,long revision) {
        var config=configurations.getAgentConfiguration(id,Map.of());
        if(!(config.get("prompt") instanceof String prompt) || prompt.isBlank()) throw new SpeakerFailure(409,"INVALID_AGENT");
        var selected=(Map<?,?>)config.get("selected_module");
        if(selected==null) throw new SpeakerFailure(409,"AGENT_MODEL_REQUIRED");
        for(String type:new String[]{"VAD","ASR","LLM","TTS","Memory","Intent"}) {
            Object key=selected.get(type);
            var model=key==null?null:models.getModelByIdFromCache(key.toString());
            if(model==null || !type.equals(model.getModelType()) || model.getConfigJson()==null)
                throw new SpeakerFailure(409,"AGENT_MODEL_REQUIRED");
            boolean noOperation=type.equals("Intent") && "nointent".equals(model.getConfigJson().getStr("type"))
                    || type.equals("Memory") && "nomem".equals(model.getConfigJson().getStr("type"));
            if(!noOperation && !Integer.valueOf(1).equals(model.getIsEnabled()))
                throw new SpeakerFailure(409,"AGENT_MODEL_REQUIRED");
        }
        if(marketModels!=null) marketModels.validate(config);
        return config;
    }
}
