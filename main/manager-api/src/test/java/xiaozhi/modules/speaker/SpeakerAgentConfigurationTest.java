package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import java.util.*;
import cn.hutool.json.JSONObject;
import org.junit.jupiter.api.Test;
import xiaozhi.modules.config.service.ConfigService;
import xiaozhi.modules.model.entity.ModelConfigEntity;
import xiaozhi.modules.model.service.ModelConfigService;

class SpeakerAgentConfigurationTest {
    @Test void acceptsOriginalProvidersWithoutAModelNameAndRejectsDisabledConfiguration() {
        var configurations=mock(ConfigService.class);var models=mock(ModelConfigService.class);
        var selected=new LinkedHashMap<String,String>();
        var config=new LinkedHashMap<String,Object>();config.put("prompt","完整提示词");config.put("selected_module",selected);
        when(configurations.getAgentConfiguration("fixture",Map.of())).thenReturn(config);
        ModelConfigEntity asr=null;
        for(String type:List.of("VAD","ASR","LLM","TTS","Memory","Intent")) {
            selected.put(type,type);
            var model=new ModelConfigEntity();model.setId(type);model.setModelType(type);model.setIsEnabled(1);
            model.setConfigJson(new JSONObject(Map.of("type",type.equals("ASR")?"fun_local":"original")));
            when(models.getModelByIdFromCache(type)).thenReturn(model);
            if(type.equals("ASR")) asr=model;
        }
        var validator=new SpeakerAgentConfiguration(configurations,models);
        assertSame(config,validator.resolve("fixture",1));
        asr.setIsEnabled(0);
        assertEquals("AGENT_MODEL_REQUIRED",assertThrows(SpeakerFailure.class,()->validator.resolve("fixture",1)).getMessage());
    }
}
