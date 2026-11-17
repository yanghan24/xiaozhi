package xiaozhi.modules.devicecontrol;

import java.util.*;
import org.apache.shiro.authz.annotation.RequiresPermissions;
import org.springframework.http.ResponseEntity;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.*;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.multipart.MultipartFile;
import xiaozhi.modules.security.user.SecurityUser;
import xiaozhi.common.utils.Result;

@RestController
@RequestMapping("/admin/product-ota")
public class ProductOtaCatalogController {
    private final ProductOtaCatalog catalog;
    public ProductOtaCatalogController(ProductOtaCatalog catalog) { this.catalog = catalog; }
    private void operator() {
        catalog.operator(SecurityUser.getUserId());
    }
    @GetMapping("/firmware")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> list() {
        operator();
        return ok(catalog.list());
    }
    @GetMapping("/github-releases")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> releases(@RequestParam(defaultValue="1") int page) { operator(); return ok(catalog.releases(page)); }
    private final ObjectMapper json=new ObjectMapper().enable(JsonParser.Feature.STRICT_DUPLICATE_DETECTION)
            .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS);
    @PostMapping("/firmware")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> importFirmware(@RequestBody String raw) {
        operator();
        JsonNode body;
        try { body=json.readTree(raw); } catch(Exception invalid) { throw new ControlFailure(400,"INVALID_REQUEST"); }
        if(body==null || !body.isObject() || body.size()!=2 || !body.path("release_id").isIntegralNumber()
                || !body.path("asset_id").isIntegralNumber() || !body.path("release_id").canConvertToLong()
                || !body.path("asset_id").canConvertToLong()) throw new ControlFailure(400,"INVALID_REQUEST");
        return ok(catalog.importFirmware(SecurityUser.getUserId(),body.path("release_id").asLong(),body.path("asset_id").asLong()));
    }
    @PostMapping(value = "/firmware/upload", consumes = "multipart/form-data")
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> uploadFirmware(@RequestPart("image") MultipartFile image,
                                             @RequestPart("manifest") MultipartFile manifest) {
        operator();
        return ok(catalog.uploadFirmware(SecurityUser.getUserId(), image, manifest));
    }
    private ResponseEntity<?> ok(Object data) {
        return ResponseEntity.ok().header("Cache-Control", "no-store").body(new Result<>().ok(data));
    }
}
