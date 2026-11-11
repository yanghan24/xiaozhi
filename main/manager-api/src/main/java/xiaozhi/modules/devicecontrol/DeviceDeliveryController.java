package xiaozhi.modules.devicecontrol;

import java.util.Collections;
import java.util.Map;
import java.util.Set;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.apache.shiro.authz.annotation.RequiresPermissions;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import xiaozhi.modules.security.user.SecurityUser;

@RestController
@RequestMapping("/device-delivery/v1")
public class DeviceDeliveryController {
    private final DeviceDeliveryService service;
    private final ObjectMapper json = new ObjectMapper()
            .enable(JsonParser.Feature.STRICT_DUPLICATE_DETECTION)
            .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS);

    public DeviceDeliveryController(DeviceDeliveryService service) { this.service = service; }

    @PostMapping(value = "/authorize", consumes = "application/json")
    @RequiresPermissions("sys:role:superAdmin")
    public JsonNode authorize(HttpServletRequest request, HttpServletResponse response, @RequestBody String raw) {
        prepare(request, response);
        body(raw, Set.of("version"));
        return success(service.authorize(SecurityUser.getUser()));
    }

    @PostMapping(value = "/operations", consumes = "application/json")
    public JsonNode create(HttpServletRequest request, HttpServletResponse response, @RequestBody String raw) {
        long operator = tool(request, response);
        var body = body(raw, Set.of("version", "hardware_id", "network_hardware_id", "board_type"));
        return success(service.create(operator, request.getHeader("Idempotency-Key"), text(body, "hardware_id"),
                text(body, "network_hardware_id"), text(body, "board_type")));
    }

    @GetMapping("/operations/{id}")
    public JsonNode get(HttpServletRequest request, HttpServletResponse response, @PathVariable String id) {
        return success(service.get(tool(request, response), id));
    }

    @PostMapping(value = "/operations/{id}/complete", consumes = "application/json")
    public JsonNode complete(HttpServletRequest request, HttpServletResponse response,
            @PathVariable String id, @RequestBody String raw) {
        long operator = tool(request, response);
        var body = body(raw, Set.of("version", "write_operation_id"));
        return success(service.complete(operator, id, text(body, "write_operation_id")));
    }

    @PostMapping(value = "/device-check", consumes = "application/json")
    public JsonNode checkDevice(HttpServletRequest request, HttpServletResponse response, @RequestBody String raw) {
        prepare(request, response);
        var body = body(raw, Set.of("version", "hardware_id", "network_hardware_id"));
        return success(service.checkDevice(request.getHeader("Authorization"), request.getHeader("Device-Id"),
                text(body, "hardware_id"), text(body, "network_hardware_id")));
    }

    private long tool(HttpServletRequest request, HttpServletResponse response) {
        prepare(request, response);
        return service.authenticateTool(request.getHeader("Authorization"));
    }

    private void prepare(HttpServletRequest request, HttpServletResponse response) {
        response.setHeader("Cache-Control", "no-store");
        if (!request.isSecure()) throw new ControlFailure(403, "HTTPS_REQUIRED");
        if (request.getQueryString() != null) throw new ControlFailure(400, "INVALID_REQUEST");
        for (String header : Set.of("Authorization", "Device-Id", "Idempotency-Key", "Content-Type")) {
            if (Collections.list(request.getHeaders(header)).size() > 1) throw new ControlFailure(400, "INVALID_REQUEST");
        }
    }

    private JsonNode body(String raw, Set<String> fields) {
        try {
            JsonNode body = json.readTree(raw);
            if (body == null || !body.isObject() || body.size() != fields.size()
                    || !body.path("version").isIntegralNumber() || !body.path("version").canConvertToInt()
                    || body.path("version").intValue() != 1) {
                throw new ControlFailure(400, "INVALID_REQUEST");
            }
            body.fieldNames().forEachRemaining(field -> {
                if (!fields.contains(field)) throw new ControlFailure(400, "INVALID_REQUEST");
            });
            return body;
        } catch (com.fasterxml.jackson.core.JsonProcessingException invalid) {
            throw new ControlFailure(400, "INVALID_REQUEST");
        }
    }

    private String text(JsonNode body, String field) {
        if (!body.path(field).isTextual()) throw new ControlFailure(400, "INVALID_REQUEST");
        return body.path(field).textValue();
    }

    private JsonNode success(Map<String, Object> data) { return json.valueToTree(Map.of("version", 1, "code", "OK", "data", data)); }
}
