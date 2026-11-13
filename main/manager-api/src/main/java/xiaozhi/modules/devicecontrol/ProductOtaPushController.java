package xiaozhi.modules.devicecontrol;

import org.apache.shiro.authz.annotation.RequiresPermissions;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import xiaozhi.modules.security.user.SecurityUser;
import xiaozhi.common.utils.Result;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.*;
import java.util.*;

@RestController
@RequestMapping("/admin/product-ota")
public class ProductOtaPushController {
    private final ProductOtaCatalog catalog;
    private final ProductOtaPush push;
    private final ObjectMapper json=new ObjectMapper().enable(JsonParser.Feature.STRICT_DUPLICATE_DETECTION)
            .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS);
    public ProductOtaPushController(ProductOtaCatalog catalog,ProductOtaPush push) { this.catalog=catalog; this.push=push; }
    @GetMapping("/devices")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> devices(@RequestParam(defaultValue="1") int page,@RequestParam(defaultValue="50") int limit) {
        catalog.operator(SecurityUser.getUserId()); return ok(push.devices(page,limit));
    }
    @PostMapping(value="/preview",consumes="application/json")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> preview(@RequestBody String raw) {
        catalog.operator(SecurityUser.getUserId());
        var body=parse(raw,Set.of("device_id","firmware_id"));
        return ok(push.preview(SecurityUser.getUserId(),body.path("device_id").asText(),body.path("firmware_id").asText()));
    }
    @PostMapping(value="/operations",consumes="application/json")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> confirm(@RequestBody String raw) {
        catalog.operator(SecurityUser.getUserId()); var body=parse(raw,Set.of("preview_id","request_key"));
        return ok(push.confirm(SecurityUser.getUserId(),body.path("preview_id").asText(),body.path("request_key").asText()));
    }
    @GetMapping("/operations")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> operations() { catalog.operator(SecurityUser.getUserId()); return ok(push.operations()); }
    @GetMapping("/operations/{id}")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> operation(@PathVariable String id) { catalog.operator(SecurityUser.getUserId()); return ok(push.observe(id)); }
    private JsonNode parse(String raw,Set<String> fields) {
        try {
            var body=json.readTree(raw); var actual=new HashSet<String>(); body.fieldNames().forEachRemaining(actual::add);
            if(!body.isObject() || !fields.equals(actual)) throw new IllegalArgumentException();
            for(String field:fields) if(!body.path(field).isTextual()) throw new IllegalArgumentException();
            return body;
        } catch(Exception invalid) { throw new ControlFailure(400,"INVALID_REQUEST"); }
    }
    private ResponseEntity<?> ok(Object data) {
        return ResponseEntity.ok().header("Cache-Control","no-store").body(new Result<>().ok(data));
    }
}
