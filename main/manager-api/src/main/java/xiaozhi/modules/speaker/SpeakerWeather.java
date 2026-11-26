package xiaozhi.modules.speaker;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.google.common.net.InetAddresses;
import java.io.IOException;
import java.math.BigDecimal;
import java.net.Inet4Address;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutionException;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;
import org.springframework.web.util.UriComponentsBuilder;

/** Standby weather for authenticated devices. Credentials and provider responses stay on the server. */
@Service
public class SpeakerWeather {
    private static final String AMAP = "https://restapi.amap.com";
    // Leave time to return the result before the device's eight-second timeout.
    private static final Duration STANDBY_BUDGET = Duration.ofSeconds(7);

    public record Report(String status, String city, String condition, String temperature,
                         String humidity, String wind_direction, String wind_power, String reported_at,
                         @JsonInclude(JsonInclude.Include.NON_NULL) BigDecimal feelsLike,
                         @JsonInclude(JsonInclude.Include.NON_NULL) Integer aqi) {
        public Report(String status, String city, String condition, String temperature, String humidity,
                      String wind_direction, String wind_power, String reported_at) {
            this(status, city, condition, temperature, humidity, wind_direction, wind_power, reported_at, null, null);
        }

        Report withStandbyDetails(BigDecimal feelsLike, Integer aqi) {
            return new Report(status, city, condition, temperature, humidity, wind_direction, wind_power, reported_at,
                    feelsLike, aqi);
        }

        static Report unavailable() {
            return new Report("unavailable", "", "", "", "", "", "", "");
        }
    }

    private record Lookup(long configurationVersion, String value) {}
    private record Location(String code, String city) {}

    private final SpeakerThirdPartyServices services;
    private final ObjectMapper json;
    private final HttpClient http;
    private final QWeatherClient qweather;
    private final Cache<Lookup, Location> locations = CacheBuilder.newBuilder()
            .maximumSize(4096).expireAfterWrite(Duration.ofHours(6)).build();
    private final Cache<Lookup, Report> weather = CacheBuilder.newBuilder()
            .maximumSize(1024).expireAfterWrite(Duration.ofMinutes(20)).build();

    @Autowired
    public SpeakerWeather(SpeakerThirdPartyServices services, ObjectMapper json) {
        this(services, json, HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(2)).build());
    }

    SpeakerWeather(SpeakerThirdPartyServices services, ObjectMapper json, HttpClient http) {
        this.services = services;
        this.json = json;
        this.http = http;
        this.qweather = new QWeatherClient(json, http);
    }

    public Report current(String address) {
        long deadline = System.nanoTime() + STANDBY_BUDGET.toNanos();
        var configured = services.qweatherConfiguration();
        if (configured.isPresent()) {
            var config = configured.get();
            String city = deviceCity(address, config.defaultCity());
            if (city.isEmpty()) return Report.unavailable();
            try {
                return qweather.standby(config, city, deadline);
            } catch (ExecutionException failure) {
                return Report.unavailable();
            }
        }
        if (!publicIpv4(address)) {
            return Report.unavailable();
        }
        var config = services.amapConfiguration();
        if (config.isEmpty()) {
            return Report.unavailable();
        }
        var selected = config.get();
        try {
            Location location = locations.get(new Lookup(selected.version(), address),
                    () -> locate(selected.credential(), address));
            return weather.get(new Lookup(selected.version(), location.code()),
                    () -> fetchWeather(selected.credential(), location));
        } catch (ExecutionException unavailable) {
            return Report.unavailable();
        }
    }

    public Map<String, Object> query(String address, String requestedCity) {
        var configured = services.qweatherConfiguration();
        if (configured.isEmpty()) return Map.of("status", "unavailable");
        var config = configured.get();
        String city = requestedCity != null && !requestedCity.isBlank() ? requestedCity.strip()
                : deviceCity(address, config.defaultCity());
        if (city.isEmpty()) return Map.of("status", "city_required");
        if (city.length() > 64 || city.chars().anyMatch(Character::isISOControl)) return Map.of("status", "unavailable");
        try {
            var current = qweather.current(config, city);
            List<QWeatherClient.Day> forecast;
            try {
                forecast = qweather.forecast(config, city);
            } catch (ExecutionException failure) {
                forecast = List.of();
            }
            return Map.of("status", "ready", "current", current, "forecast", forecast);
        } catch (ExecutionException failure) {
            return Map.of("status", "unavailable");
        }
    }

    private String deviceCity(String address, String defaultCity) {
        if (publicIpv4(address)) {
            var locator = services.amapConfiguration();
            if (locator.isPresent()) {
                var config = locator.get();
                try {
                    var location = locations.get(new Lookup(config.version(), address),
                            () -> locate(config.credential(), address));
                    if (!location.city().isEmpty()) return location.city();
                } catch (ExecutionException ignored) {
                    // Continue with the configured city when IP lookup is unavailable.
                }
            }
        }
        return defaultCity == null ? "" : defaultCity;
    }

    static boolean publicIpv4(String value) {
        if (value == null || value.length() > 15 || !InetAddresses.isInetAddress(value)
                || !(InetAddresses.forString(value) instanceof Inet4Address ipv4)) {
            return false;
        }
        byte[] bytes = ipv4.getAddress();
        int first = Byte.toUnsignedInt(bytes[0]);
        int second = Byte.toUnsignedInt(bytes[1]);
        return first > 0 && first < 224 && !ipv4.isLoopbackAddress() && !ipv4.isSiteLocalAddress()
                && !ipv4.isLinkLocalAddress() && !(first == 100 && second >= 64 && second <= 127)
                && !(first == 198 && (second == 18 || second == 19));
    }

    private Location locate(String key, String address) throws IOException, InterruptedException {
        JsonNode result = request("/v3/ip", key, Map.of("ip", address));
        String code = text(result, "adcode");
        if (!code.matches("[0-9]{6}")) {
            throw new IOException("City unavailable");
        }
        String city = text(result, "city");
        return new Location(code, city.isEmpty() ? text(result, "province") : city);
    }

    private Report fetchWeather(String key, Location location) throws IOException, InterruptedException {
        JsonNode live = request("/v3/weather/weatherInfo", key,
                Map.of("city", location.code(), "extensions", "base")).path("lives").path(0);
        String condition = text(live, "weather");
        if (condition.isEmpty()) {
            throw new IOException("Weather unavailable");
        }
        String city = text(live, "city");
        return new Report("ready", city.isEmpty() ? location.city() : city, condition, text(live, "temperature"),
                text(live, "humidity"), text(live, "winddirection"), text(live, "windpower"), text(live, "reporttime"));
    }

    private JsonNode request(String path, String key, Map<String, String> parameters)
            throws IOException, InterruptedException {
        var builder = UriComponentsBuilder.fromUriString(AMAP).path(path).queryParam("key", "{key}");
        parameters.forEach((name, value) -> builder.queryParam(name, "{" + name + "}"));
        var values = new java.util.HashMap<>(parameters);
        values.put("key", key);
        URI uri = builder.encode().buildAndExpand(values).toUri();
        var request = HttpRequest.newBuilder(uri).timeout(Duration.ofSeconds(3)).GET().build();
        var response = http.send(request, HttpResponse.BodyHandlers.ofByteArray());
        if (response.statusCode() != 200 || response.body().length > 32768) {
            throw new IOException("Weather service unavailable");
        }
        JsonNode body = json.readTree(response.body());
        if (body == null || !"1".equals(body.path("status").asText())) {
            throw new IOException("Weather request failed");
        }
        return body;
    }

    static String text(JsonNode node, String name) {
        JsonNode value = node.path(name);
        if (!value.isTextual()) return "";
        String text = value.textValue().strip();
        return text.length() <= 64 && text.chars().noneMatch(Character::isISOControl) ? text : "";
    }
}
