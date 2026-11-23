package xiaozhi.modules.model.market;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import java.util.*;
import cn.hutool.json.JSONObject;
import com.fasterxml.jackson.databind.*;
import org.junit.jupiter.api.*;
import org.springframework.jdbc.core.JdbcTemplate;
import xiaozhi.modules.model.entity.ModelConfigEntity;
import xiaozhi.modules.model.service.ModelConfigService;
import xiaozhi.modules.speaker.*;

class MarketModelRuntimeTest {
    MarketModelCatalog catalog=mock(MarketModelCatalog.class);
    MarketResourceCredentials keys=mock(MarketResourceCredentials.class);
    JdbcTemplate db=mock(JdbcTemplate.class);
    ModelConfigService models=mock(ModelConfigService.class);
    MarketModelRuntime runtime=new MarketModelRuntime(catalog,keys,db,models);
    @BeforeEach void setup() throws Exception {
        var json=new ObjectMapper();
        when(catalog.list()).thenReturn(List.of(json.readTree("""
            {"publicId":"compatible-chat","protocol":"openai",
             "capabilities":["chat","streaming","tools","vision","structured-output"],
             "verifiedCapabilities":["chat","streaming","tools","vision","structured-output"],
             "verifiedMethods":{"vision":["chat-image-data-uri"],"structured-output":["json_object"]}}
            """),json.readTree("""
            {"publicId":"spark_zh_iat_new","protocol":"xfyun_asr_v2",
             "capabilities":["transcription"],"verifiedCapabilities":["transcription"]}
            """)));
        for(String kind:List.of("ASR","LLM","VLLM","Intent","Memory")) {
            var model=new ModelConfigEntity();model.setIsEnabled(1);
            when(models.getModelByIdFromCache(MarketModelBindings.id(kind))).thenReturn(model);
        }
        when(keys.forCustomer("a")).thenReturn(new SpeakerMarket.Credential("user-a-key","https://aimarket.hncig.cn",null));
        when(keys.forCustomer("b")).thenReturn(new SpeakerMarket.Credential("user-b-key","https://aimarket.hncig.cn",null));
    }
    Map<String,Object> configuration() {
        return new HashMap<>(Map.of("selected_module",Map.of("LLM","MARKET_LLM","Intent","MARKET_Intent"),
                "LLM",Map.of("MARKET_LLM",Map.of("source","aimarket","type","openai","model_name","compatible-chat")),
                "VLLM",Map.of("MARKET_VLLM",Map.of("source","aimarket","type","openai","model_name","compatible-chat")),
                "ASR",Map.of("MARKET_ASR",Map.of("source","aimarket","type","aimarket_xfyun_asr_v2","model_name","spark_zh_iat_new")),
                "Intent",Map.of("MARKET_Intent",Map.of("source","aimarket","type","intent_llm","model_name","compatible-chat","llm","market_intent_llm")),
                "Memory",Map.of("MARKET_Memory",Map.of("source","aimarket","type","mem_local_short","model_name","compatible-chat","llm","market_memory_llm"))));
    }
    @Test void isolatesTwoCustomersAndBuildsOriginalAuxiliaryModelsWithoutMutatingSharedConfig() {
        var original=configuration();
        var a=runtime.configure(original,"a");var b=runtime.configure(original,"b");
        assertTrue(a.toString().contains("user-a-key"));assertFalse(a.toString().contains("user-b-key"));
        assertTrue(b.toString().contains("user-b-key"));assertFalse(b.toString().contains("user-a-key"));
        assertFalse(original.toString().contains("api_key"));assertFalse(original.toString().contains("/v1"));
        assertTrue(a.toString().contains("market_intent_llm"));assertTrue(a.toString().contains("json_object"));
        assertTrue(a.toString().contains("https://aimarket.hncig.cn/v1"));
        var runtimeConfig=new ObjectMapper().valueToTree(a);
        for(String id:List.of("MARKET_LLM","market_intent_llm","market_memory_llm")) {
            assertEquals("none",runtimeConfig.path("LLM").path(id).path("reasoning_effort").asText());
        }
        assertEquals("none",runtimeConfig.at("/VLLM/MARKET_VLLM/reasoning_effort").asText());
        assertTrue(runtimeConfig.at("/ASR/MARKET_ASR/reasoning_effort").isMissingNode());
        assertFalse(original.toString().contains("reasoning_effort"));
    }
    @Test void systemOnlyConfigurationDoesNotNeedCatalogOrResourceKey() {
        var system=Map.<String,Object>of("LLM",Map.of("original",Map.of("type","openai","api_key","system-key")));
        assertEquals(system,runtime.configure(system,null));verifyNoInteractions(keys,catalog);
    }
    @Test void injectsEachCustomersKeyIntoNativeTtsAndKeepsTheSelectedVoice() throws Exception {
        when(catalog.list()).thenReturn(List.of(new ObjectMapper().readTree("""
            {"publicId":"super-smart-tts","protocol":"xfyun_super_tts",
             "capabilities":["speech"],"verifiedCapabilities":["speech"],
             "verifiedMethods":{"speech":["xfyun-tts-stream"]}}
            """)));
        var model = new ModelConfigEntity();
        model.setIsEnabled(1);
        when(models.getModelByIdFromCache("MARKET_TTS")).thenReturn(model);
        var tts = Map.of("source","aimarket","type","xunfei_stream","model_name","super-smart-tts",
                "private_voice","x6_lingfeiyi_pro");
        var config = Map.<String,Object>of("TTS", Map.of("MARKET_TTS", tts));
        for (String customer : List.of("a", "b")) {
            var resolved = new ObjectMapper().valueToTree(runtime.configure(config, customer)).at("/TTS/MARKET_TTS");
            assertEquals("user-"+customer+"-key", resolved.path("api_key").asText());
            assertEquals("https://aimarket.hncig.cn", resolved.path("base_url").asText());
            assertEquals("x6_lingfeiyi_pro", resolved.path("private_voice").asText());
            assertTrue(resolved.path("reasoning_effort").isMissingNode());
        }
        assertFalse(tts.containsKey("api_key"));
    }
    @Test void functionCallRequiresToolsAndUnavailableSelectionIsNotSilentlyChanged() throws Exception {
        var config=configuration();
        config.put("Intent",Map.of("original",Map.of("type","function_call")));
        config.put("selected_module",Map.of("LLM","MARKET_LLM","Intent","original"));
        var model=new ObjectMapper().readTree("""
            {"publicId":"compatible-chat","protocol":"openai","capabilities":["chat","streaming"],"verifiedCapabilities":["chat","streaming"]}
            """);
        when(catalog.list()).thenReturn(List.of(model));
        assertThrows(SpeakerFailure.class,()->runtime.configure(config,"a"));verifyNoInteractions(keys);
    }
    @Test void javaCallsRequireOwnedActiveConversationAndNeverModifyCachedPublicConfig() {
        var publicConfig=new JSONObject(Map.of("source","aimarket","model_name","compatible-chat"));
        assertThrows(SpeakerFailure.class,()->runtime.forConversation(publicConfig,"LLM",null));
        when(db.queryForList(anyString(),eq("owned"))).thenReturn(List.of(Map.of("customer_id","b")));
        var resolved=runtime.forConversation(publicConfig,"LLM","owned");
        assertEquals("user-b-key",resolved.getStr("api_key"));assertFalse(publicConfig.containsKey("api_key"));
        assertEquals("none",resolved.getStr("reasoning_effort"));
        assertFalse(publicConfig.containsKey("reasoning_effort"));
        assertThrows(SpeakerFailure.class,()->runtime.forConversation(publicConfig,"LLM","not-owned"));
    }
}
