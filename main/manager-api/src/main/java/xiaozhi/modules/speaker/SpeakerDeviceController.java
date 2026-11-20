package xiaozhi.modules.speaker;

import jakarta.validation.Valid;
import jakarta.validation.constraints.Size;
import jakarta.servlet.http.HttpServletRequest;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.web.bind.annotation.*;
import java.util.Map;

@RestController
@RequestMapping("/speaker/v1")
@ConditionalOnProperty(name="speaker.enabled", havingValue="true")
public class SpeakerDeviceController {
    private final SpeakerDevices devices;
    private final SpeakerLogins logins;
    private final SpeakerOperations operations;
    private final SpeakerWeather weather;
    public SpeakerDeviceController(SpeakerDevices devices,SpeakerLogins logins,SpeakerOperations operations,
            SpeakerWeather weather) {
        this.devices = devices; this.logins=logins; this.operations=operations;
        this.weather = weather;
    }
    public record Heartbeat(@Size(max=128) String board_type, @Size(max=128) String firmware_version,
                            boolean diagnostics_supported) {}
    @PostMapping("/heartbeat")
    public Map<String,Object> heartbeat(@RequestHeader(name="Authorization",required=false) String auth,
            @RequestHeader(name="Device-Id",required=false) String id, @Valid @RequestBody Heartbeat body) {
        return devices.heartbeat(devices.authenticate(auth,id), body.board_type(), body.firmware_version(),
                body.diagnostics_supported());
    }
    @PostMapping("/login")
    public Object login(@RequestHeader(name="Authorization",required=false) String auth,
            @RequestHeader(name="Device-Id",required=false) String id) {
        return logins.begin(devices.authenticateForLogin(auth,id));
    }
    @GetMapping("/login/{transaction}")
    public Object loginStatus(@RequestHeader(name="Authorization",required=false) String auth,
            @RequestHeader(name="Device-Id",required=false) String id,@PathVariable String transaction) {
        return logins.status(devices.authenticateForLogin(auth,id),transaction);
    }
    @GetMapping("/state")
    public Object state(@RequestHeader(name="Authorization",required=false) String auth,
            @RequestHeader(name="Device-Id",required=false) String id) {
        return operations.state(devices.authenticate(auth,id));
    }
    @GetMapping("/weather")
    public SpeakerWeather.Report weather(@RequestHeader(name="Authorization",required=false) String auth,
            @RequestHeader(name="Device-Id",required=false) String id, HttpServletRequest request) {
        var device = devices.load(SpeakerDevices.parseIdentity(auth, id), false);
        if (!"authorized".equals(operations.useStatus(device))) {
            throw new SpeakerFailure(403, "DEVICE_WEATHER_UNAVAILABLE");
        }
        String address = "public".equals(request.getHeader("X-Xiaozhi-Ingress"))
                ? request.getHeader("X-Xiaozhi-Device-IP") : request.getRemoteAddr();
        return weather.current(address);
    }
    public record Action(long expected_permission_version,String request_key) {}
    @PostMapping("/actions/{action}")
    public Object action(@RequestHeader(name="Authorization",required=false) String auth,
            @RequestHeader(name="Device-Id",required=false) String id,@PathVariable String action,@RequestBody Action body) {
        return operations.action(devices.authenticate(auth,id),action,body.expected_permission_version(),body.request_key(),"device");
    }
}
