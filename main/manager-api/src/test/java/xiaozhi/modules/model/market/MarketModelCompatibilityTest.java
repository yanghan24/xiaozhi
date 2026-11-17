package xiaozhi.modules.model.market;

import static org.junit.jupiter.api.Assertions.*;
import java.util.*;
import com.fasterxml.jackson.databind.*;
import org.junit.jupiter.api.Test;
import xiaozhi.modules.speaker.SpeakerFailure;

class MarketModelCompatibilityTest {
    private final ObjectMapper json=new ObjectMapper();
    private JsonNode model(String protocol,String...caps) {
        var m=json.createObjectNode();m.put("publicId","changing-model-name");m.put("protocol",protocol);
        m.set("capabilities",json.valueToTree(caps));m.set("verifiedCapabilities",json.valueToTree(caps));
        m.set("verifiedMethods",json.valueToTree(Map.of("vision",List.of("chat-image-data-uri"),"structured-output",List.of("json_object"))));
        return m;
    }
    @Test void filtersByContractsRatherThanHardcodedChatNames() {
        var m=model("openai","chat","streaming","tools","vision","structured-output");
        for(String type:List.of("LLM","VLLM","Intent","Memory")) assertTrue(MarketModelCompatibility.supports(type,m));
        assertFalse(MarketModelCompatibility.supports("LLM",model("openai","chat","vision")));
        assertTrue(MarketModelCompatibility.supports("VLLM",model("openai","chat","vision")));
        assertFalse(MarketModelCompatibility.supports("LLM",model("other","chat","streaming")));
    }
    @Test void requiresVerifiedImageMethodAndOptionalToolContract() {
        var m=model("openai","chat","streaming","vision");
        assertThrows(SpeakerFailure.class,()->MarketModelCompatibility.require("LLM","changing-model-name",List.of(m),true));
        ((com.fasterxml.jackson.databind.node.ObjectNode)m).remove("verifiedMethods");
        assertFalse(MarketModelCompatibility.supports("VLLM",m));
        assertFalse(MarketModelCompatibility.supports("Intent",m));
        assertTrue(MarketModelCompatibility.supports("Memory",m));
    }
    @Test void chatOnlyAndUnverifiedToolsNeverBecomeMainLlmChoices() {
        var plain=model("openai","chat","streaming");
        assertFalse(MarketModelCompatibility.supports("LLM",plain));
        assertThrows(SpeakerFailure.class,()->MarketModelCompatibility.require("LLM","changing-model-name",List.of(plain),false));
        assertTrue(MarketModelCompatibility.supports("Memory",plain));
        var claimed=model("openai","chat","streaming","tools");
        ((com.fasterxml.jackson.databind.node.ObjectNode)claimed).set("verifiedCapabilities",json.valueToTree(List.of("chat","streaming")));
        assertFalse(MarketModelCompatibility.supports("LLM",claimed));
    }
    @Test void refusesUnverifiedAndOutOfScopeAsrEvenIfPublished() {
        var m=model("xfyun_asr_v2","transcription");
        assertFalse(MarketModelCompatibility.supports("ASR",m));
        ((com.fasterxml.jackson.databind.node.ObjectNode)m).put("publicId","spark_zh_iat_new");
        assertTrue(MarketModelCompatibility.supports("ASR",m));
        ((com.fasterxml.jackson.databind.node.ObjectNode)m).put("protocol","xfyun_rtasr_llm");
        assertFalse(MarketModelCompatibility.supports("ASR",m));
        ((com.fasterxml.jackson.databind.node.ObjectNode)m).remove("verifiedCapabilities");
        assertFalse(MarketModelCompatibility.supports("ASR",m));
        for(String type:List.of("VAD","TTS","RAG")) assertFalse(MarketModelCompatibility.supports(type,model("openai","chat","streaming")));
    }
}
