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
@RequestMapping("/admin/speaker/function-plugins")
@RequiresPermissions("sys:role:superAdmin")
@ConditionalOnProperty(name = "speaker.enabled", havingValue = "true")
public class SpeakerFunctionPluginController {
    private final SpeakerFunctionPlugins plugins;

    public SpeakerFunctionPluginController(SpeakerFunctionPlugins plugins) {
        this.plugins = plugins;
    }

    @GetMapping
    public List<SpeakerFunctionPlugins.Entry> list() {
        return plugins.list();
    }

    @GetMapping("/choices")
    public List<SpeakerFunctionPlugins.Choice> choices() {
        return plugins.choices();
    }

    @PostMapping("/{id}")
    public Map<String, Boolean> save(@PathVariable String id, @RequestBody SpeakerFunctionPlugins.Input input) {
        plugins.save(id, input);
        return Map.of("saved", true);
    }
}
