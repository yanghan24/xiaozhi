package xiaozhi.modules.model.market;

import java.util.Map;
import org.apache.shiro.authz.annotation.RequiresPermissions;
import org.springframework.web.bind.annotation.*;
import org.springframework.http.ResponseEntity;
import xiaozhi.modules.speaker.SpeakerFailure;

@RestController
@RequestMapping("/models/market")
@RequiresPermissions("sys:role:superAdmin")
public class MarketModelController {
    private final MarketModelBindings bindings;
    public MarketModelController(MarketModelBindings bindings) { this.bindings=bindings; }
    @GetMapping("/{type}") public Object configuration(@PathVariable String type) { return bindings.binding(type); }
    @GetMapping("/{type}/options") public ResponseEntity<?> options(@PathVariable String type) {
        return ResponseEntity.ok().header("Cache-Control","no-store").body(Map.of("items",bindings.options(type)));
    }
    public record Selection(String publicId,boolean enabled) {}
    @PutMapping("/{type}") public Object save(@PathVariable String type,@RequestBody Selection selection) {
        if(selection.publicId()==null || selection.publicId().isBlank()) throw new SpeakerFailure(400,"MODEL_REQUIRED");
        return bindings.save(type,selection.publicId(),selection.enabled());
    }
    @ExceptionHandler(SpeakerFailure.class) public ResponseEntity<?> failure(SpeakerFailure error) {
        return ResponseEntity.status(error.status).header("Cache-Control","no-store").body(Map.of("code",error.getMessage()));
    }
}
