package xiaozhi.modules.speaker;

import java.util.List;
import org.apache.shiro.authz.annotation.RequiresPermissions;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/admin/speaker")
@RequiresPermissions("sys:role:superAdmin")
@ConditionalOnProperty(name="speaker.enabled",havingValue="true")
public class SpeakerMcpController {
    private final SpeakerMcp services;
    public SpeakerMcpController(SpeakerMcp services) { this.services=services; }
    @GetMapping("/mcp") public Object list() { return services.list(); }
    @PostMapping("/mcp") public Object create(@RequestBody SpeakerMcp.Configuration body) { return services.save(null,body); }
    @PostMapping("/mcp/{id}") public Object save(@PathVariable String id,@RequestBody SpeakerMcp.Configuration body) { return services.save(id,body); }
    @GetMapping("/agent-mcp/{id}") public Object selected(@PathVariable String id) { return services.selected(id); }
    @PostMapping("/agent-mcp/{id}") public Object select(@PathVariable String id,@RequestBody List<String> body) { return services.select(id,body); }
}
