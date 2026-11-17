package xiaozhi.modules.model.market;

import java.util.*;
import com.fasterxml.jackson.databind.JsonNode;
import xiaozhi.modules.speaker.SpeakerFailure;

/** Protocol contracts, not a whitelist of model names. */
public final class MarketModelCompatibility {
    private MarketModelCompatibility() {}
    public static String type(String value) {
        return switch(value.toLowerCase(Locale.ROOT)) {
            case "llm","slm" -> "LLM"; case "vllm" -> "VLLM"; case "asr" -> "ASR";
            case "intent" -> "Intent"; case "memory" -> "Memory";
            case "vad" -> "VAD"; case "tts" -> "TTS"; case "rag" -> "RAG";
            default -> throw new SpeakerFailure(400,"INVALID_MODEL_TYPE");
        };
    }
    public static boolean has(JsonNode model,String capability) {
        return contains(model.path("capabilities"),capability) && contains(model.path("verifiedCapabilities"),capability);
    }
    private static boolean contains(JsonNode values,String value) {
        for(var node:values) if(node.isTextual() && value.equals(node.asText())) return true;
        return false;
    }
    public static boolean supports(String kind,JsonNode model) {
        String type=type(kind);
        if(type.equals("ASR")) return model.path("publicId").asText().equals("spark_zh_iat_new")
                && model.path("protocol").asText().equals("xfyun_asr_v2") && has(model,"transcription");
        if(!model.path("protocol").asText().equals("openai") || !has(model,"chat")) return false;
        if(type.equals("VLLM")) return has(model,"vision")
                && contains(model.path("verifiedMethods").path("vision"),"chat-image-data-uri");
        if(!Set.of("LLM","Intent","Memory").contains(type) || !has(model,"streaming")) return false;
        if(type.equals("LLM")) return has(model,"tools");
        if(type.equals("Memory")) return true;
        return has(model,"structured-output")
                && contains(model.path("verifiedMethods").path("structured-output"),"json_object");
    }
    public static JsonNode require(String type,String publicId,List<JsonNode> models,boolean tools) {
        return models.stream().filter(m -> publicId.equals(m.path("publicId").asText()))
                .filter(m -> supports(type,m) && (!tools || has(m,"tools"))).findFirst()
                .orElseThrow(() -> new SpeakerFailure(409,"MARKET_MODEL_INCOMPATIBLE"));
    }
}
