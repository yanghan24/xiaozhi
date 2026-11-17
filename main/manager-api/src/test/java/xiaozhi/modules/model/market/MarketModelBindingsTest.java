package xiaozhi.modules.model.market;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import java.util.*;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.Test;
import org.mockito.ArgumentCaptor;
import org.springframework.jdbc.core.JdbcTemplate;
import xiaozhi.common.redis.RedisUtils;
import xiaozhi.modules.model.entity.ModelConfigEntity;
import xiaozhi.modules.model.service.ModelConfigService;
import xiaozhi.modules.speaker.SpeakerFailure;

class MarketModelBindingsTest {
    @Test void savesOneStableAdapterWithoutPersonalKeysAndRejectsBypass() throws Exception {
        var models=mock(ModelConfigService.class);var catalog=mock(MarketModelCatalog.class);
        var redis=mock(RedisUtils.class);var db=mock(JdbcTemplate.class);
        var service=new MarketModelBindings(models,catalog,redis,db);
        var candidate=new ObjectMapper().readTree("""
                {"publicId":"future-model","protocol":"openai","capabilities":["chat","streaming"],
                 "verifiedCapabilities":["chat","streaming"],"api_key":"upstream-must-not-leak"}
                """);
        when(catalog.list()).thenReturn(List.of(candidate));
        service.save("llm","future-model",true);
        var saved=ArgumentCaptor.forClass(ModelConfigEntity.class);
        verify(models).insert(saved.capture());
        assertEquals("MARKET_LLM",saved.getValue().getId());
        assertEquals("future-model",saved.getValue().getConfigJson().getStr("model_name"));
        assertFalse(saved.getValue().getConfigJson().containsKey("api_key"));
        verify(db).update(contains("a.id=r.configured_agent_id"),eq("MARKET_LLM"),eq("MARKET_LLM"));
        assertThrows(SpeakerFailure.class,()->service.save("vllm","future-model",true));
        assertThrows(SpeakerFailure.class,()->service.save("llm","unlisted-model",true));
        verify(models,times(1)).insert(any());
    }

    @Test void permitsDisablingSavedAdapterDuringDirectoryOutage() {
        var models=mock(ModelConfigService.class);var catalog=mock(MarketModelCatalog.class);
        var service=new MarketModelBindings(models,catalog,mock(RedisUtils.class),mock(JdbcTemplate.class));
        var model=new ModelConfigEntity();model.setId("MARKET_LLM");model.setIsDefault(0);model.setIsEnabled(1);
        model.setConfigJson(new cn.hutool.json.JSONObject(Map.of("source","aimarket","model_name","saved")));
        when(models.selectById("MARKET_LLM")).thenReturn(model);
        service.save("LLM","saved",false);
        verifyNoInteractions(catalog);assertEquals(0,model.getIsEnabled());
        model.setIsDefault(1);
        assertThrows(SpeakerFailure.class,()->service.save("LLM","saved",false));
    }
}
