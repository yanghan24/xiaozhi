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
    private final ProductOtaBatches batches;
    private final ObjectMapper json=new ObjectMapper().enable(JsonParser.Feature.STRICT_DUPLICATE_DETECTION)
            .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS);
    public ProductOtaPushController(ProductOtaCatalog catalog,ProductOtaPush push,ProductOtaBatches batches) { this.catalog=catalog; this.push=push; this.batches=batches; }
    @PostMapping(value="/batch-preview",consumes="application/json")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> batchPreview(@RequestBody String raw) {
        long operator=SecurityUser.getUserId(); catalog.operator(operator);
        try {
            var body=json.readTree(raw); var fields=new HashSet<String>(); body.fieldNames().forEachRemaining(fields::add);
            if(!body.isObject() || !fields.equals(Set.of("mode","device_ids","firmware_id"))
                    || !body.path("mode").isTextual() || !body.path("firmware_id").isTextual() || !body.path("device_ids").isArray()) throw new IllegalArgumentException();
            var devices=new ArrayList<String>();
            for(var device:body.path("device_ids")) { if(!device.isTextual()) throw new IllegalArgumentException(); devices.add(device.asText()); }
            return ok(batches.preview(operator,body.path("mode").asText(),devices,body.path("firmware_id").asText()));
        } catch(ControlFailure failure) { throw failure; }
        catch(Exception invalid) { throw new ControlFailure(400,"INVALID_REQUEST"); }
    }
    @PostMapping(value="/batches",consumes="application/json")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> batchConfirm(@RequestBody String raw) {
        long operator=SecurityUser.getUserId(); catalog.operator(operator);
        var body=parse(raw,Set.of("preview_id","request_key"));
        return ok(batches.confirm(operator,body.path("preview_id").asText(),body.path("request_key").asText()));
    }
    @GetMapping("/batch-previews/{id}")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> batchPreviewStatus(@PathVariable String id) { long operator=SecurityUser.getUserId(); catalog.operator(operator); return ok(batches.preview(operator,id)); }
    @GetMapping("/batches")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> batches() { long operator=SecurityUser.getUserId(); catalog.operator(operator); return ok(batches.batches(operator)); }
    @GetMapping("/batches/{id}")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> batch(@PathVariable String id) { long operator=SecurityUser.getUserId(); catalog.operator(operator); return ok(batches.batch(operator,id)); }
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
