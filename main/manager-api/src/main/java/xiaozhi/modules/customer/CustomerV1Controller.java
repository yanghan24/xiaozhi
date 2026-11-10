package xiaozhi.modules.customer;

import java.util.Map;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.http.converter.HttpMessageNotReadableException;
import org.springframework.web.HttpMediaTypeNotSupportedException;
import org.springframework.web.bind.annotation.*;

/** Product transport adapter; the compatibility entry uses the same identity service. */
@RestController
@RequestMapping("/customer/v1")
public class CustomerV1Controller {
    private final CustomerIdentityService identities;
    private final CustomerBindingService bindings;
    private final CustomerDeviceSessionService sessions;
    private final ObjectMapper json = new ObjectMapper()
            .enable(JsonParser.Feature.STRICT_DUPLICATE_DETECTION)
            .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS);

    CustomerV1Controller(CustomerIdentityService identities, CustomerBindingService bindings, CustomerDeviceSessionService sessions) {
        this.identities = identities; this.bindings=bindings; this.sessions=sessions;
    }

    @PostMapping({"/binding-flows", "/binding-flows/{id}/confirm", "/device-sessions/{id}/sign-out", "/binding-flows/{id}/open-account"})
    ResponseEntity<Map<String,Object>> unsupportedMedia() {
        // Keep unmatched media types out of the management exception envelope.
        return failure(new CustomerFailure(400,"INVALID_REQUEST"));
    }

    @PostMapping(value="/binding-flows/{id}/confirm", consumes=MediaType.APPLICATION_JSON_VALUE)
    ResponseEntity<com.fasterxml.jackson.databind.JsonNode> confirm(
            @CookieValue(name="customer_browser", required=false) String cookie,
            @RequestHeader(name="X-CSRF-Token", required=false) String csrf,
            @RequestHeader(name="Origin", required=false) String origin,
            @RequestHeader(name="Idempotency-Key", required=false) String key,
            @PathVariable String id, @RequestBody String raw) {
        try {
            var body=json.readTree(raw);
            if (!body.isObject() || !body.isEmpty()) throw new IllegalArgumentException();
        } catch (Exception invalid) { throw new CustomerFailure(400,"INVALID_REQUEST"); }
        var data=bindings.confirm(cookie,csrf,origin,id,key);
        // Protocol versions are JSON numbers, independent of management Long serialization.
        return ResponseEntity.ok().header("Cache-Control","no-store")
                .body(json.valueToTree(Map.of("version",1,"code","OK","data",data)));
    }

    @PostMapping(value="/binding-flows/{id}/confirm", consumes=MediaType.APPLICATION_FORM_URLENCODED_VALUE)
    ResponseEntity<?> confirmForm(@CookieValue(name="customer_browser", required=false) String cookie,
            @RequestHeader(name="Origin", required=false) String origin, @PathVariable String id,
            @RequestParam String csrf, @RequestParam String idempotency_key) {
        try { bindings.confirm(cookie,csrf,origin,id,idempotency_key); }
        catch (CustomerFailure rejected) {
            String message=switch(rejected.getMessage()) {
                case "DEVICE_ALREADY_BOUND" -> "设备已被绑定，请联系原归属账号。";
                case "REQUEST_EXPIRED", "FLOW_EXPIRED" -> "设备请求已过期，请重新扫码。";
                default -> "绑定未完成，身份或设备状态已变化，请重新扫码。";
            };
            return ResponseEntity.status(rejected.status).contentType(new MediaType("text","html",java.nio.charset.StandardCharsets.UTF_8))
                    .header("Cache-Control","no-store").header("Content-Security-Policy","default-src 'none'; frame-ancestors 'none'")
                    .body("<!doctype html><html lang=\"zh-CN\"><meta charset=\"UTF-8\"><title>绑定未完成</title><h1>"+message+"</h1></html>");
        }
        return ResponseEntity.status(303).header("Cache-Control","no-store").header("Location","/xiaozhi/customer/results/"+id).build();
    }

    @PostMapping(value="/binding-flows", consumes=MediaType.APPLICATION_JSON_VALUE)
    ResponseEntity<Map<String,Object>> create(
            @CookieValue(name="customer_browser", required=false) String cookie,
            @RequestHeader(name="X-CSRF-Token", required=false) String csrf,
            @RequestHeader(name="Origin", required=false) String origin, @RequestBody String raw) {
        identities.browser(cookie);
        String handle;
        try {
            var body = json.readTree(raw);
            if (!body.isObject() || body.size()!=1 || !body.path("request_handle").isTextual())
                throw new IllegalArgumentException();
            handle=body.get("request_handle").textValue();
            if (handle.isEmpty() || handle.length()>256) throw new IllegalArgumentException();
        } catch (Exception invalid) { throw new CustomerFailure(400,"INVALID_REQUEST"); }
        return success(201, identities.createFlow(cookie, csrf, origin, handle));
    }

    @PostMapping(value="/binding-flows", consumes=MediaType.APPLICATION_FORM_URLENCODED_VALUE)
    ResponseEntity<Void> createForm(@CookieValue(name="customer_browser", required=false) String cookie,
            @RequestHeader(name="Origin", required=false) String origin,
            @RequestParam String csrf, @RequestParam String request_handle) {
        identities.browser(cookie);
        var data=identities.createFlow(cookie, csrf, origin, request_handle);
        return ResponseEntity.status(303).header("Cache-Control","no-store")
                .header("Location",data.get("authorization_url").toString()).build();
    }

    @GetMapping("/binding-flows/{id}")
    ResponseEntity<Map<String,Object>> status(@CookieValue(name="customer_browser", required=false) String cookie,
            @PathVariable String id) {
        return success(200, identities.flowStatus(cookie,id));
    }

    @PostMapping(value="/device-sessions/{id}/sign-out", consumes=MediaType.APPLICATION_JSON_VALUE)
    ResponseEntity<com.fasterxml.jackson.databind.JsonNode> signOut(
            @CookieValue(name="customer_browser",required=false) String cookie,
            @RequestHeader(name="X-CSRF-Token",required=false) String csrf,
            @RequestHeader(name="Origin",required=false) String origin,
            @RequestHeader(name="Idempotency-Key",required=false) String key,
            @PathVariable String id,@RequestBody String raw) {
        try { var body=json.readTree(raw);if(!body.isObject() || !body.isEmpty()) throw new IllegalArgumentException(); }
        catch(Exception invalid) { throw new CustomerFailure(400,"INVALID_REQUEST"); }
        return ResponseEntity.ok().header("Cache-Control","no-store")
                .body(json.valueToTree(Map.of("version",1,"code","OK","data",sessions.signOut(cookie,csrf,origin,id,key))));
    }
    @GetMapping("/device-operations/{id}")
    ResponseEntity<com.fasterxml.jackson.databind.JsonNode> operation(
            @CookieValue(name="customer_browser",required=false) String cookie,@PathVariable String id) {
        return ResponseEntity.ok().header("Cache-Control","no-store")
                .body(json.valueToTree(Map.of("version",1,"code","OK","data",sessions.operation(cookie,id))));
    }
    @PostMapping(value="/binding-flows/{id}/open-account", consumes=MediaType.APPLICATION_JSON_VALUE)
    ResponseEntity<com.fasterxml.jackson.databind.JsonNode> openAccount(
            @CookieValue(name="customer_browser",required=false) String cookie,
            @RequestHeader(name="X-CSRF-Token",required=false) String csrf,
            @RequestHeader(name="Origin",required=false) String origin,
            @RequestHeader(name="Idempotency-Key",required=false) String key,
            @PathVariable String id,@RequestBody String raw) {
        try { var body=json.readTree(raw);if(!body.isObject() || !body.isEmpty()) throw new IllegalArgumentException(); }
        catch(Exception invalid) { throw new CustomerFailure(400,"INVALID_REQUEST"); }
        return ResponseEntity.ok().header("Cache-Control","no-store")
                .body(json.valueToTree(Map.of("version",1,"code","OK","data",bindings.openAccount(cookie,csrf,origin,id,key))));
    }
    @PostMapping(value="/binding-flows/{id}/open-account", consumes=MediaType.APPLICATION_FORM_URLENCODED_VALUE)
    ResponseEntity<?> openAccountForm(@CookieValue(name="customer_browser",required=false) String cookie,
            @RequestHeader(name="Origin",required=false) String origin,@PathVariable String id,
            @RequestParam String csrf,@RequestParam String idempotency_key) {
        try {
            var result=bindings.openAccount(cookie,csrf,origin,id,idempotency_key);
            return deviceRedirect(result.get("device_session_id").toString());
        } catch(CustomerFailure rejected) { return actionError(rejected); }
    }
    @PostMapping(value="/device-sessions/{id}/sign-out", consumes=MediaType.APPLICATION_FORM_URLENCODED_VALUE)
    ResponseEntity<?> signOutForm(@CookieValue(name="customer_browser",required=false) String cookie,
            @RequestHeader(name="Origin",required=false) String origin,@PathVariable String id,
            @RequestParam String csrf,@RequestParam String idempotency_key) {
        try { sessions.signOut(cookie,csrf,origin,id,idempotency_key);return deviceRedirect(id); }
        catch(CustomerFailure rejected) { return actionError(rejected); }
    }
    private ResponseEntity<Void> deviceRedirect(String id) {
        return ResponseEntity.status(303).header("Cache-Control","no-store").header("Location","/xiaozhi/customer/devices/"+id).build();
    }
    private ResponseEntity<String> actionError(CustomerFailure rejected) {
        String message="OWNER_REQUIRED".equals(rejected.getMessage())?"请使用原归属账号操作。":"操作未完成，身份或设备状态已变化，请重新扫码。";
        return ResponseEntity.status(rejected.status).contentType(new MediaType("text","html",java.nio.charset.StandardCharsets.UTF_8))
                .header("Cache-Control","no-store").header("Content-Security-Policy","default-src 'none'; frame-ancestors 'none'")
                .body("<!doctype html><html lang=\"zh-CN\"><meta charset=\"UTF-8\"><title>操作未完成</title><h1>"+message+"</h1></html>");
    }
    private ResponseEntity<Map<String,Object>> success(int status, Map<String,Object> data) {
        return ResponseEntity.status(status).header("Cache-Control","no-store")
                .body(Map.of("version",1,"code","OK","data",data));
    }

    @ExceptionHandler(CustomerFailure.class)
    ResponseEntity<Map<String,Object>> failure(CustomerFailure failure) {
        String code = switch (failure.getMessage()) {
            case "IDENTITY_REQUIRED" -> "CUSTOMER_AUTH_REQUIRED";
            case "FLOW_ALREADY_USED", "REQUEST_CONSUMED" -> "STATE_CHANGED";
            case "CSRF_REJECTED", "REQUEST_NOT_FOUND", "REQUEST_EXPIRED", "FLOW_NOT_FOUND",
                    "FLOW_EXPIRED", "STATE_CHANGED", "INVALID_REQUEST", "DEVICE_ALREADY_BOUND", "PURPOSE_MISMATCH", "OWNER_REQUIRED" -> failure.getMessage();
            default -> "TEMPORARILY_UNAVAILABLE";
        };
        int status = code.equals("TEMPORARILY_UNAVAILABLE") ? 503 : failure.status;
        return ResponseEntity.status(status).header("Cache-Control","no-store")
                .body(Map.of("version",1,"code",code,"message",code));
    }

    @ExceptionHandler({HttpMessageNotReadableException.class, HttpMediaTypeNotSupportedException.class,
            org.springframework.web.bind.MissingServletRequestParameterException.class})
    ResponseEntity<Map<String,Object>> invalid(Exception failure) {
        return failure(new CustomerFailure(400,"INVALID_REQUEST"));
    }

    @ExceptionHandler(Exception.class)
    ResponseEntity<Map<String,Object>> unavailable(Exception failure) {
        return failure(new CustomerFailure(503,"TEMPORARILY_UNAVAILABLE"));
    }
}
