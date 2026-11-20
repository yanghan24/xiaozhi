package xiaozhi.modules.model.market;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import cn.hutool.json.JSONObject;
import cn.hutool.json.JSONUtil;
import com.sun.net.httpserver.HttpServer;
import org.junit.jupiter.api.Test;
import org.springframework.test.util.ReflectionTestUtils;
import xiaozhi.modules.llm.service.impl.OpenAIStyleLLMServiceImpl;
import xiaozhi.modules.model.entity.ModelConfigEntity;
import xiaozhi.modules.model.service.ModelConfigService;

class MarketJavaHelpersTest {
    @Test void originalSummaryAndTitleUseExplicitConversationCredentialsWithoutPublicKeyFallback() throws Exception {
        var server=HttpServer.create(new InetSocketAddress("127.0.0.1",0),0);
        var authorizations=new ArrayList<String>();
        var requests=new ArrayList<JSONObject>();
        var status=new java.util.concurrent.atomic.AtomicInteger(200);
        server.createContext("/v1/chat/completions",exchange -> {
            authorizations.add(exchange.getRequestHeaders().getFirst("Authorization"));
            requests.add(JSONUtil.parseObj(new String(exchange.getRequestBody().readAllBytes(),StandardCharsets.UTF_8)));
            byte[] bytes="{\"choices\":[{\"message\":{\"content\":\"fixture summary\"}}]}".getBytes(StandardCharsets.UTF_8);
            exchange.getResponseHeaders().set("Content-Type","application/json");
            exchange.sendResponseHeaders(status.get(),bytes.length);
            try(var out=exchange.getResponseBody()) {out.write(bytes);}
        });
        server.start();
        try {
            var models=mock(ModelConfigService.class);var runtime=mock(MarketModelRuntime.class);
            var model=new ModelConfigEntity();model.setId("MARKET_LLM");model.setModelType("LLM");
            model.setConfigJson(new JSONObject(Map.of("source","aimarket","model_name","fixture")));
            when(models.getModelByIdFromCache("MARKET_LLM")).thenReturn(model);
            when(runtime.forConversation(any(),eq("LLM"),anyString())).thenAnswer(call -> new JSONObject(Map.of(
                    "base_url","http://127.0.0.1:"+server.getAddress().getPort()+"/v1",
                    "model_name","fixture","api_key","key-for-"+call.getArgument(2),
                    "source","aimarket","reasoning_effort","none")));
            var service=new OpenAIStyleLLMServiceImpl();
            ReflectionTestUtils.setField(service,"modelConfigService",models);
            ReflectionTestUtils.setField(service,"marketModels",runtime);
            assertEquals("fixture summary",service.generateSummary("test","prompt","MARKET_LLM","conversation-a"));
            assertEquals("fixture summary",service.generateSummaryWithHistory("test","history","prompt","MARKET_LLM","conversation-b"));
            assertEquals("fixture summary",service.generateTitle("test","MARKET_LLM","conversation-a"));
            assertEquals(List.of("Bearer key-for-conversation-a","Bearer key-for-conversation-b","Bearer key-for-conversation-a"),authorizations);
            assertEquals(List.of("none","none","none"),requests.stream().map(body->body.getStr("reasoning_effort")).toList());
            assertFalse(model.getConfigJson().containsKey("api_key"));
            status.set(503);
            assertThrows(xiaozhi.modules.speaker.SpeakerFailure.class,()->service.generateSummaryWithHistory("test","history","prompt","MARKET_LLM","conversation-a"));
            verify(models,never()).getEnabledModelsByType(anyString());
        } finally {server.stop(0);}
    }
}
