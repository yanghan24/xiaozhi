package xiaozhi.modules.speaker;

import java.util.Map;
import org.apache.shiro.authz.annotation.RequiresPermissions;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.web.bind.annotation.*;
import xiaozhi.modules.security.user.SecurityUser;

@RestController
@RequestMapping("/admin/speaker")
@RequiresPermissions("sys:role:superAdmin")
@ConditionalOnProperty(name="speaker.enabled", havingValue="true")
public class SpeakerAdminController {
    private final SpeakerDevices devices;
    private final SpeakerCustomers customers;
    private final SpeakerOperations operations;
    private final SpeakerAgents agents;
    public SpeakerAdminController(SpeakerDevices devices,SpeakerCustomers customers,SpeakerOperations operations,SpeakerAgents agents) {
        this.devices = devices; this.customers=customers; this.operations=operations; this.agents=agents;
    }
    @GetMapping("/devices")
    public Object devices(@RequestParam(defaultValue="") String keyword, @RequestParam(defaultValue="1") int page,
            @RequestParam(defaultValue="50") int limit) {
        return Map.of("items",devices.list(keyword,page,limit),"total",devices.count(keyword));
    }
    @GetMapping("/devices/{id}")
    public Object device(@PathVariable String id) { return devices.detail(id); }
    public record Registration(String device_id,String name,String board_type) {}
    @PostMapping("/devices")
    public Object register(@RequestBody Registration body) {
        return devices.register(body.device_id(),body.name(),body.board_type(),SecurityUser.getUserId());
    }
    @GetMapping("/customers")
    public Object customers(@RequestParam(defaultValue="") String keyword,@RequestParam(defaultValue="1") int page,
            @RequestParam(defaultValue="50") int limit) { return customers.list(keyword,page,limit); }
    @GetMapping("/customers/{id}")
    public Object customer(@PathVariable String id) { return customers.detail(id); }
    @PostMapping("/customers/{id}/refresh")
    public Object refreshCustomer(@PathVariable String id) { return customers.refresh(id); }
    public record Reauthorization(long expected_permission_version) {}
    @PostMapping("/customers/{id}/devices/{device}/reauthorization")
    public Object requestReauthorization(@PathVariable String id,@PathVariable String device,@RequestBody Reauthorization body) {
        return operations.requestReauthorization(id,device,body.expected_permission_version(),SecurityUser.getUserId());
    }
    public record DeviceAction(long expected_permission_version,String request_key) {}
    @PostMapping("/devices/{id}/{action}")
    public Object action(@PathVariable String id,@PathVariable String action,@RequestBody DeviceAction body) {
        return operations.adminAction(id,action,body.expected_permission_version(),body.request_key(),SecurityUser.getUserId());
    }
    @GetMapping("/agent-default") public Object agentDefault() { return agents.defaultConfiguration(); }
    @PostMapping("/agent-default/{id}") public Object selectConfiguration(@PathVariable String id,@RequestBody DefaultAgent config) {
        return agents.selectConfiguration(id,config.expected_revision());
    }
    public record DefaultAgent(long expected_revision) {}
}
