package xiaozhi.modules.devicecontrol;

import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URI;
import java.util.Map;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.servlet.http.HttpServletRequest;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;

/** Fetch the device view from the operator's private weather43 cache. */
@RestController
public class DeviceEnvironmentController {
    private final URI endpoint;
    private final ObjectMapper json = new ObjectMapper();
    private static final int MAX_BYTES = 16 * 1024;

    public DeviceEnvironmentController(@Value("${device-control.weather-origin:}") String origin) {
        if (origin.isBlank()) {
            endpoint = null;
            return;
        }
        URI base = URI.create(origin);
        if (!("http".equals(base.getScheme()) || "https".equals(base.getScheme()))
                || base.getHost() == null || base.getRawUserInfo() != null
                || !base.getRawPath().isEmpty() || base.getRawQuery() != null || base.getRawFragment() != null) {
            throw new IllegalArgumentException("device-control.weather-origin requires a private HTTP(S) origin");
        }
        endpoint = base.resolve("/internal/device-environment");
    }

    @GetMapping("/device-control/v1/environment")
    public JsonNode environment(HttpServletRequest request) throws IOException {
        var query = json.createObjectNode();
        String cityId = request.getParameter("city_id");
        String city = request.getParameter("city");
        String province = request.getParameter("province");
        if (cityId != null) {
            if (!cityId.matches("[1-9][0-9]{0,9}") || city != null || province != null) throw invalid();
            query.put("city_id", cityId);
        } else if (city != null) {
            query.put("city", name(city));
            if (province != null) query.put("province", name(province));
        } else {
            if (province != null) throw invalid();
            // Tomcat's trusted proxy valve supplies the observed client address.
            query.put("client_ip", request.getRemoteAddr());
        }
        if (endpoint == null) throw new ControlFailure(503, "WEATHER_UNAVAILABLE");
        var connection = (HttpURLConnection) endpoint.toURL().openConnection();
        try {
            connection.setConnectTimeout(1500);
            connection.setReadTimeout(3000);
            connection.setInstanceFollowRedirects(false);
            connection.setRequestMethod("POST");
            connection.setRequestProperty("Content-Type", "application/json");
            connection.setDoOutput(true);
            byte[] body = json.writeValueAsBytes(query);
            connection.setFixedLengthStreamingMode(body.length);
            try (var output = connection.getOutputStream()) {
                output.write(body);
            }
            if (connection.getResponseCode() != 200) throw new ControlFailure(503, "WEATHER_UNAVAILABLE");
            try (var input = connection.getInputStream()) {
                byte[] response = input.readNBytes(MAX_BYTES + 1);
                if (response.length > MAX_BYTES) throw new ControlFailure(503, "WEATHER_UNAVAILABLE");
                var data = json.readTree(response);
                if (data == null || !data.isObject() || !data.path("location_status").isTextual()) {
                    throw new ControlFailure(503, "WEATHER_UNAVAILABLE");
                }
                return json.valueToTree(Map.of("version", 1, "code", "OK", "data", data));
            }
        } catch (IOException unavailable) {
            throw new ControlFailure(503, "WEATHER_UNAVAILABLE");
        } finally {
            connection.disconnect();
        }
    }

    private static String name(String value) {
        if (value.isBlank() || value.length() > 64 || value.chars().anyMatch(Character::isISOControl)) throw invalid();
        return value.strip();
    }

    private static ControlFailure invalid() {
        return new ControlFailure(400, "INVALID_REQUEST");
    }
}
