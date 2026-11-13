package xiaozhi.modules.customer;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Map;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.DeserializationFeature;

/** Private backend-only authority. Never exposed by the device-entry proxy. */
@RestController
@RequestMapping("/internal/device-voice/v1")
final class CustomerVoiceController {
    private final CustomerVoicePermissions permissions;
    private final String serviceKey;
    private final ProductAsrSettings asr;
    private final ObjectMapper json=new ObjectMapper().enable(JsonParser.Feature.STRICT_DUPLICATE_DETECTION).enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS);
    CustomerVoiceController(CustomerVoicePermissions permissions,@Value("${device-control.voice-service-secret:}") String serviceKey,ProductAsrSettings asr) {
        this.permissions=permissions;this.serviceKey=serviceKey;this.asr=asr;
    }
    @PostMapping("/{action}")
    ResponseEntity<?> unsupportedMedia(){return failure(new CustomerFailure(400,"INVALID_REQUEST"));}
    @PostMapping(value="/{action}",consumes="application/json")
    ResponseEntity<?> action(@PathVariable String action,@RequestHeader(name="X-Voice-Service-Key",required=false) String key,@RequestBody String raw) {
        if(serviceKey.length()<32 || key==null || !MessageDigest.isEqual(serviceKey.getBytes(StandardCharsets.UTF_8),key.getBytes(StandardCharsets.UTF_8)))
            throw new CustomerFailure(401,"VOICE_DENIED");
        if(!permissions.enabled()) throw new CustomerFailure(503,"VOICE_UNAVAILABLE");
        if(raw.length()>2048) throw new CustomerFailure(400,"INVALID_REQUEST");
        com.fasterxml.jackson.databind.JsonNode body;
        try {body=json.readTree(raw);}catch(Exception invalid){throw new CustomerFailure(400,"INVALID_REQUEST");}
        boolean open="open".equals(action);
        boolean rejected="key-rejected".equals(action);
        if(!java.util.Set.of("open","check","close","model-access","asr-access","key-rejected").contains(action) || !body.isObject() || body.size()!=(open?2:1)) throw new CustomerFailure(400,"INVALID_REQUEST");
        Map<String,Object> result;
        if(open) result=permissions.open(field(body,"device_id"),field(body,"token"));
        else if(rejected) {
            var receipt=body.path("rejection_receipt");
            if(!receipt.isTextual() || receipt.asText().length()>1024)throw new CustomerFailure(400,"INVALID_REQUEST");
            result=permissions.keyRejected(receipt.asText());
        }
        else {
            String session=field(body,"session");
            if("close".equals(action)){permissions.close(session);result=Map.of("closed",true);}
            else if("asr-access".equals(action)) {
                permissions.check(session);
                result=asr.access();
                permissions.check(session);
            }
            else result="model-access".equals(action)?permissions.modelAccess(session):permissions.check(session);
        }
        return ResponseEntity.ok().header("Cache-Control","no-store").body(json.valueToTree(result));
    }
    private String field(com.fasterxml.jackson.databind.JsonNode body,String field) {
        var value=body.path(field);if(!value.isTextual() || value.asText().isBlank() || value.asText().length()>128)throw new CustomerFailure(400,"INVALID_REQUEST");return value.asText();
    }
    @ExceptionHandler(CustomerFailure.class)
    ResponseEntity<?> failure(CustomerFailure failure){return ResponseEntity.status(failure.status).header("Cache-Control","no-store").body(Map.of("code",failure.getMessage()));}
    @ExceptionHandler(Exception.class)
    ResponseEntity<?> unavailable(Exception failure){return ResponseEntity.status(503).header("Cache-Control","no-store").body(Map.of("code","VOICE_UNAVAILABLE"));}
}
