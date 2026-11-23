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
                {"publicId":"future-model","protocol":"openai","capabilities":["chat","streaming","tools"],
                 "verifiedCapabilities":["chat","streaming","tools"],"api_key":"upstream-must-not-leak"}
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
    @Test void excludesOldIncompatibleAdapterAndPreservesSystemOptionsDuringOutage() throws Exception {
        var models=mock(ModelConfigService.class);var catalog=mock(MarketModelCatalog.class);
        var service=new MarketModelBindings(models,catalog,mock(RedisUtils.class),mock(JdbcTemplate.class));
        var system=new xiaozhi.modules.model.dto.ModelBasicInfoDTO();system.setId("system");
        var market=new xiaozhi.modules.model.dto.ModelBasicInfoDTO();market.setId("MARKET_LLM");
        var adapter=new ModelConfigEntity();adapter.setModelType("LLM");adapter.setIsEnabled(1);
        adapter.setConfigJson(new cn.hutool.json.JSONObject(Map.of("source","aimarket","model_name","plain")));
        when(models.selectById("MARKET_LLM")).thenReturn(adapter);
        when(catalog.list()).thenReturn(List.of(new ObjectMapper().readTree("""
            {"publicId":"plain","protocol":"openai","capabilities":["chat","streaming"],"verifiedCapabilities":["chat","streaming"]}
            """)));
        assertEquals(List.of(system),service.compatibleChoices(List.of(system,market)));
        assertThrows(SpeakerFailure.class,()->service.validateSelections(Map.of("LLM","MARKET_LLM")));
        when(catalog.list()).thenThrow(new SpeakerFailure(503,"MARKET_CATALOG_UNAVAILABLE"));
        assertEquals(List.of(system),service.compatibleChoices(List.of(system,market)));
    }
    @Test void savingMainModelCannotBypassToolsRequirementButMemoryCanUseChat() throws Exception {
        var models=mock(ModelConfigService.class);var catalog=mock(MarketModelCatalog.class);
        var service=new MarketModelBindings(models,catalog,mock(RedisUtils.class),mock(JdbcTemplate.class));
        when(catalog.list()).thenReturn(List.of(new ObjectMapper().readTree("""
            {"publicId":"plain","protocol":"openai","capabilities":["chat","streaming"],"verifiedCapabilities":["chat","streaming"]}
            """)));
        assertThrows(SpeakerFailure.class,()->service.save("LLM","plain",true));
        service.save("Memory","plain",true);
        verify(models,times(1)).insert(any());
    }
    @Test void roleCannotPutAnAsrAdapterInTheMainLlmField() {
        var models=mock(ModelConfigService.class);var catalog=mock(MarketModelCatalog.class);
        var service=new MarketModelBindings(models,catalog,mock(RedisUtils.class),mock(JdbcTemplate.class));
        var asr=new ModelConfigEntity();asr.setModelType("ASR");asr.setIsEnabled(1);
        when(models.selectById("MARKET_ASR")).thenReturn(asr);
        assertThrows(SpeakerFailure.class,()->service.validateSelections(Map.of("LLM","MARKET_ASR")));
        verifyNoInteractions(catalog);
    }
}
