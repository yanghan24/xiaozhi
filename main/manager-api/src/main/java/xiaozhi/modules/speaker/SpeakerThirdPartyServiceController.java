package xiaozhi.modules.speaker;

import java.util.List;
import java.util.Map;
import org.apache.shiro.authz.annotation.RequiresPermissions;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/admin/speaker/third-party-services")
@RequiresPermissions("sys:role:superAdmin")
@ConditionalOnProperty(name = "speaker.enabled", havingValue = "true")
public class SpeakerThirdPartyServiceController {
    private final SpeakerThirdPartyServices services;

    public SpeakerThirdPartyServiceController(SpeakerThirdPartyServices services) {
        this.services = services;
    }

    @GetMapping
    public List<SpeakerThirdPartyServices.Entry> list() {
        return services.list();
    }

    @PostMapping("/{id}")
    public Map<String, Boolean> save(@PathVariable String id,
                                     @RequestBody SpeakerThirdPartyServices.Configuration body) {
        services.save(id, body);
        return Map.of("saved", true);
    }
}
