package xiaozhi.modules.devicecontrol;

import java.util.Map;
import java.util.Set;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.core.JsonProcessingException;
import jakarta.servlet.http.HttpServletRequest;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;

@RestController
@RequestMapping("/device-control/v1")
public class DeviceControlController {
    private final DeviceControlService service;
    private final ObjectMapper json = new ObjectMapper()
            .enable(JsonParser.Feature.STRICT_DUPLICATE_DETECTION)
            .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS);

    public DeviceControlController(DeviceControlService service) {
        this.service = service;
    }

    @GetMapping("/state")
    public JsonNode state(HttpServletRequest request) {
        Long after = queryNumber(request, "after_revision", DeviceControlService.MAX_VERSION);
        Long wait = queryNumber(request, "wait_ms", 20000);
        return success(service.poll(identity(request), after, wait == null ? 0 : wait.intValue()));
    }

    private Long queryNumber(HttpServletRequest request, String name, long max) {
        String value = request.getParameter(name);
        if (value == null) return null;
        if (!value.matches("[0-9]{1,16}")) throw new ControlFailure(400, "INVALID_REQUEST");
        long number = Long.parseLong(value);
        if (number > max) throw new ControlFailure(400, "INVALID_REQUEST");
        return number;
    }

    @PostMapping(value = "/requests", consumes = "application/json")
    public JsonNode requests(HttpServletRequest request, @RequestBody String rawBody) {
        JsonNode body = parse(rawBody);
        fields(body, Set.of("intent", "expected_permission_version"));
        if (!body.path("intent").isTextual() || !Set.of("auto", "account").contains(body.path("intent").textValue())) {
            throw new ControlFailure(400, "INVALID_REQUEST");
        }
        long version = version(body.path("expected_permission_version"));
        return success(service.createRequest(identity(request),
                request.getHeader("Idempotency-Key"), body.path("intent").textValue(), version));
    }

    private void fields(JsonNode body, Set<String> names) {
        if (!body.isObject() || body.size() != names.size()) throw new ControlFailure(400, "INVALID_REQUEST");
        body.fieldNames().forEachRemaining(name -> {
            if (!names.contains(name)) throw new ControlFailure(400, "INVALID_REQUEST");
        });
    }

    @PostMapping(value = "/state-acks", consumes = "application/json")
    public JsonNode acknowledgements(HttpServletRequest request, @RequestBody String rawBody) {
        JsonNode body = parse(rawBody);
        fields(body, Set.of("revision", "permission_version", "audio_stopped"));
        if (!body.path("audio_stopped").isBoolean()) throw new ControlFailure(400, "INVALID_REQUEST");
        return success(service.acknowledge(identity(request),
                version(body.path("revision")), version(body.path("permission_version")), body.path("audio_stopped").booleanValue()));
    }

    private long version(JsonNode value) {
        if (!value.isIntegralNumber() || !value.canConvertToLong() || value.longValue() < 0
                || value.longValue() > DeviceControlService.MAX_VERSION) throw new ControlFailure(400, "INVALID_REQUEST");
        return value.longValue();
    }

    private JsonNode success(Map<String, Object> data) {
        // The administrator MVC mapper stringifies Long IDs. Protocol safe integers must
        // remain JSON numbers; a typed tree keeps that legacy serializer out of this contract.
        return json.valueToTree(Map.of("version", 1, "code", "OK", "data", data));
    }

    private JsonNode parse(String rawBody) {
        if (rawBody.length() > 4096) throw new ControlFailure(400, "INVALID_REQUEST");
        try {
            JsonNode body = json.readTree(rawBody);
            if (body == null) throw new ControlFailure(400, "INVALID_REQUEST");
            return body;
        } catch (JsonProcessingException malformed) {
            throw new ControlFailure(400, "INVALID_REQUEST");
        }
    }

    private DeviceControlService.Identity identity(HttpServletRequest request) {
        return (DeviceControlService.Identity) request.getAttribute(DeviceCredentialFilter.IDENTITY);
    }
}
