package xiaozhi.modules.devicecontrol;

import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.*;
import jakarta.servlet.http.HttpServletRequest;
import java.util.Map;
import org.springframework.web.bind.annotation.*;

@RestController
public class DeviceMaintenanceController {
    private final DeviceMaintenance service;
    private final ObjectMapper json=new ObjectMapper().enable(JsonParser.Feature.STRICT_DUPLICATE_DETECTION)
            .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS);
    public DeviceMaintenanceController(DeviceMaintenance service) { this.service=service; }

    @PostMapping(value="/device-control/v1/maintenance",consumes="application/json")
    public JsonNode exchange(HttpServletRequest request,@RequestBody String raw) {
        JsonNode body;
        try { body=json.readTree(raw); }
        catch(Exception invalid) { throw new ControlFailure(400,"INVALID_REQUEST"); }
        var identity=(DeviceControlService.Identity)request.getAttribute(DeviceCredentialFilter.IDENTITY);
        return json.valueToTree(Map.of("version",1,"code","OK","data",service.exchange(identity,body)));
    }
}
