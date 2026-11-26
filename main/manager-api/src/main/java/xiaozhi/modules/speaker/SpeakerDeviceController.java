package xiaozhi.modules.speaker;

import jakarta.validation.Valid;
import jakarta.validation.constraints.Size;
import jakarta.servlet.http.HttpServletRequest;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.http.CacheControl;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import java.time.Instant;
import java.time.ZoneId;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Locale;

@RestController
@RequestMapping("/speaker/v1")
@ConditionalOnProperty(name="speaker.enabled", havingValue="true")
public class SpeakerDeviceController {
    @Value("${device-control.time-zone:Asia/Shanghai}")
    private ZoneId productTimeZone;
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
        var result = new LinkedHashMap<>(devices.heartbeat(devices.authenticate(auth,id), body.board_type(),
                body.firmware_version(), body.diagnostics_supported()));
        var instant = Instant.ofEpochMilli((Long) result.get("server_time_ms"));
        result.put("timezone_offset", productTimeZone.getRules().getOffset(instant).getTotalSeconds() / 60);
        return result;
    }
    @PostMapping("/login")
    public Object login(@RequestHeader(name="Authorization",required=false) String auth,
            @RequestHeader(name="Device-Id",required=false) String id,
            @RequestParam(defaultValue="false") boolean resume) {
        return logins.begin(devices.authenticateForLogin(auth,id),resume);
    }
    @GetMapping("/login/{transaction}")
    public Object loginStatus(@RequestHeader(name="Authorization",required=false) String auth,
            @RequestHeader(name="Device-Id",required=false) String id,@PathVariable String transaction) {
        return logins.status(devices.authenticateForLogin(auth,id),transaction);
    }
    @PostMapping("/login/{transaction}/refresh")
    public Object refreshLogin(@RequestHeader(name="Authorization",required=false) String auth,
            @RequestHeader(name="Device-Id",required=false) String id,@PathVariable String transaction) {
        return logins.refresh(devices.authenticateForLogin(auth,id),transaction);
    }
    @GetMapping(SpeakerLogins.SCAN_PATH+"{transaction}")
    public ResponseEntity<?> scanLogin(@PathVariable String transaction,HttpServletRequest request) {
        if (!"GET".equals(request.getMethod()) || isPrefetch(request.getHeader("Sec-Purpose"))
                || isPrefetch(request.getHeader("Purpose"))) {
            return ResponseEntity.noContent().cacheControl(CacheControl.noStore()).build();
        }
        var destination=logins.openScan(transaction);
        if (destination==null) {
            return ResponseEntity.status(410).cacheControl(CacheControl.noStore())
                    .contentType(MediaType.parseMediaType("text/plain;charset=UTF-8"))
                    .body("二维码已使用或已失效，请在音箱上刷新二维码。");
        }
        return ResponseEntity.status(303).location(destination).cacheControl(CacheControl.noStore())
                .header("Referrer-Policy","no-referrer").build();
    }

    private static boolean isPrefetch(String value) {
        return value!=null && value.toLowerCase(Locale.ROOT).contains("prefetch");
    }
    @GetMapping("/state")
    public Object state(@RequestHeader(name="Authorization",required=false) String auth,
            @RequestHeader(name="Device-Id",required=false) String id) {
        return operations.state(devices.authenticate(auth,id));
    }
    @GetMapping("/account/balance")
    public ResponseEntity<SpeakerOperations.AccountBalance> accountBalance(
            @RequestHeader(name="Authorization", required=false) String auth,
            @RequestHeader(name="Device-Id", required=false) String id) {
        return ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .body(operations.accountBalance(devices.authenticate(auth, id)));
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
