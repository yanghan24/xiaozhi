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
    private final ObjectMapper json = new ObjectMapper()
            .enable(JsonParser.Feature.STRICT_DUPLICATE_DETECTION)
            .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS);

    CustomerV1Controller(CustomerIdentityService identities) { this.identities = identities; }

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
                    "FLOW_EXPIRED", "STATE_CHANGED", "INVALID_REQUEST" -> failure.getMessage();
            default -> "TEMPORARILY_UNAVAILABLE";
        };
        int status = code.equals("TEMPORARILY_UNAVAILABLE") ? 503 : failure.status;
        return ResponseEntity.status(status).header("Cache-Control","no-store")
                .body(Map.of("version",1,"code",code));
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
