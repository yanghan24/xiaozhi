package xiaozhi.modules.customer;

import java.util.Map;
import org.apache.shiro.authz.annotation.RequiresPermissions;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import xiaozhi.common.utils.Result;

@RestController
@RequestMapping("/admin/product-asr")
public class ProductAsrAdminController {
    private final ProductAsrSettings settings;
    public ProductAsrAdminController(ProductAsrSettings settings) { this.settings = settings; }
    @GetMapping
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> get() { return ok(settings.get()); }
    @PutMapping
    @RequiresPermissions("sys:role:superAdmin")
    public ResponseEntity<?> save(@RequestBody ProductAsrSettings.Update update) { return ok(settings.save(update)); }
    private ResponseEntity<?> ok(Map<String,Object> value) {
        return ResponseEntity.ok().header("Cache-Control", "no-store").body(new Result<>().ok(value));
    }
    @ExceptionHandler(CustomerFailure.class)
    ResponseEntity<?> failure(CustomerFailure error) {
        return ResponseEntity.status(error.status).header("Cache-Control", "no-store").body(Map.of("code", error.getMessage()));
    }
}
