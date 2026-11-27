package xiaozhi.modules.speaker;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.google.common.net.InetAddresses;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.math.BigDecimal;
import java.math.RoundingMode;
import java.net.Inet4Address;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutionException;
import java.util.zip.GZIPInputStream;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;
import org.springframework.web.util.UriComponentsBuilder;

/** Standby weather for authenticated devices. Credentials and provider responses stay on the server. */
@Service
public class SpeakerWeather {
    private static final String AMAP = "https://restapi.amap.com";
    private static final String PCONLINE = "https://whois.pconline.com.cn";
    private static final int MAX_PROVIDER_BODY_BYTES = 32768;
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(3);
    // The device waits 8 seconds. Reserve time for both Amap calls on a cold-cache fallback.
    private static final Duration PRIMARY_BUDGET = Duration.ofMillis(4500);
    private static final Duration TOTAL_BUDGET = Duration.ofMillis(7500);

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
            return new Report("unavailable", "", "", "", "", "", "", "", null, null);
        }
    }

    private enum Locator { PCONLINE, AMAP }
    private record Lookup(Locator locator, long configurationVersion, String address) {}
    private record WeatherKey(Locator locator, long locatorVersion, long weatherVersion, String city, String province) {}
    private record Location(String code, String city, String province) {}

    private final SpeakerThirdPartyServices services;
    private final ObjectMapper json;
    private final HttpClient http;
    private final QWeatherClient qweather;
    private final Cache<Lookup, Location> locations = CacheBuilder.newBuilder()
            .maximumSize(4096).expireAfterWrite(Duration.ofHours(6)).build();
    private final Cache<WeatherKey, Report> weather = CacheBuilder.newBuilder()
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
        long start = System.nanoTime();
        var configured = services.qweatherConfiguration();
        if (configured.isPresent()) {
            var config = configured.get();
            long deadline = start + TOTAL_BUDGET.toNanos();
            String city = deviceCity(address, config.defaultCity(), deadline);
            if (city.isEmpty()) return Report.unavailable();
            try {
                return qweather.standby(config, city, deadline);
            } catch (ExecutionException failure) {
                if (failure.getCause() instanceof InterruptedException) {
                    Thread.currentThread().interrupt();
                }
                return Report.unavailable();
            }
        }
        if (!publicIpv4(address)) {
            return Report.unavailable();
        }
        var apiKey = services.qweatherApiKeyConfiguration().orElse(null);
        if (apiKey != null) {
            var locatorVersion = services.pconlineConfigurationVersion().orElse(null);
            if (locatorVersion != null) {
                long deadline = start + PRIMARY_BUDGET.toNanos();
                try {
                    Location location = locations.get(new Lookup(Locator.PCONLINE, locatorVersion, address),
                            () -> locateWithPconline(address, deadline));
                    var key = new WeatherKey(Locator.PCONLINE, locatorVersion, apiKey.version(),
                            location.city(), location.province());
                    return weather.get(key, () -> fetchQweather(apiKey, location, deadline));
                } catch (ExecutionException unavailable) {
                    if (unavailable.getCause() instanceof InterruptedException) {
                        Thread.currentThread().interrupt();
                        return Report.unavailable();
                    }
                }
            }
        }

        var amap = services.amapConfiguration().orElse(null);
        if (amap == null) {
            return Report.unavailable();
        }
        long deadline = start + TOTAL_BUDGET.toNanos();
        try {
            Location location = locations.get(new Lookup(Locator.AMAP, amap.version(), address),
                    () -> locateWithAmap(amap.credential(), address, deadline));
            if (!location.code().matches("[0-9]{6}")) {
                return Report.unavailable();
            }
            var key = new WeatherKey(Locator.AMAP, amap.version(), amap.version(), location.code(), "");
            return weather.get(key, () -> fetchAmapWeather(amap.credential(), location, deadline));
        } catch (ExecutionException unavailable) {
            if (unavailable.getCause() instanceof InterruptedException) {
                Thread.currentThread().interrupt();
            }
            return Report.unavailable();
        }
    }

    public Map<String, Object> query(String address, String requestedCity) {
        var configured = services.qweatherConfiguration();
        if (configured.isEmpty()) return Map.of("status", "unavailable");
        var config = configured.get();
        String city = requestedCity != null && !requestedCity.isBlank() ? requestedCity.strip()
                : deviceCity(address, config.defaultCity(), System.nanoTime() + REQUEST_TIMEOUT.toNanos());
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

    private String deviceCity(String address, String defaultCity, long deadline) {
        if (publicIpv4(address)) {
            var locator = services.amapConfiguration();
            if (locator.isPresent()) {
                var config = locator.get();
                try {
                    var location = locations.get(new Lookup(Locator.AMAP, config.version(), address),
                            () -> locateWithAmap(config.credential(), address, deadline));
                    if (!location.city().isEmpty()) return location.city();
                } catch (ExecutionException failure) {
                    if (failure.getCause() instanceof InterruptedException) {
                        Thread.currentThread().interrupt();
                        return "";
                    }
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

    private Location locateWithPconline(String address, long deadline) throws IOException, InterruptedException {
        JsonNode result = request(PCONLINE, "/ipJson.jsp", Map.of("json", "true", "ip", address),
                Map.of(), requestTimeout(deadline));
        String city = text(result, "city");
        if (city.isEmpty()) {
            throw new IOException("PConline city unavailable");
        }
        return new Location("", city, text(result, "pro"));
    }

    private Location locateWithAmap(String key, String address, long deadline) throws IOException, InterruptedException {
        JsonNode result = requestAmap("/v3/ip", key, Map.of("ip", address), deadline);
        String code = text(result, "adcode");
        String city = text(result, "city");
        String province = text(result, "province");
        if (city.isEmpty() && province.isEmpty() && !code.matches("[0-9]{6}")) {
            throw new IOException("City unavailable");
        }
        return new Location(code, city.isEmpty() ? province : city, province);
    }

    private Report fetchQweather(SpeakerThirdPartyServices.QWeatherApiKeyConfiguration config, Location location,
                                 long deadline) throws IOException, InterruptedException {
        if (location.city().isEmpty()) {
            throw new IOException("City unavailable");
        }
        var lookup = new HashMap<>(Map.of("location", location.city(), "lang", "zh",
                "range", "cn", "number", "1"));
        if (!location.province().isEmpty() && !location.province().equals(location.city())) {
            lookup.put("adm", location.province());
        }
        JsonNode cities = requestQweather(config, "/geo/v2/city/lookup", lookup, deadline);
        if (!"200".equals(text(cities, "code"))) {
            throw new IOException("QWeather city lookup failed");
        }
        JsonNode city = cities.path("location").path(0);
        String coordinates = "/" + coordinate(city, "lat", 90) + "/" + coordinate(city, "lon", 180);
        JsonNode live = requestQweather(config, "/weather/v1/current" + coordinates,
                Map.of("lang", "zh", "localTime", "true"), deadline);
        String condition = text(live.path("condition"), "text");
        BigDecimal temperature = celsius(live.path("temperature"));
        if (condition.isEmpty() || temperature == null) {
            throw new IOException("QWeather live weather unavailable");
        }
        JsonNode air = requestQweather(config, "/airquality/v1/current" + coordinates,
                Map.of("lang", "zh"), deadline);
        JsonNode indexes = air.path("indexes");
        if (!indexes.isArray()) {
            throw new IOException("QWeather air quality response invalid");
        }
        JsonNode humidity = live.path("humidity");
        JsonNode wind = live.path("wind");
        Integer scale = wholeNumber(wind.path("scale"), 17);
        String name = text(city, "name");
        return new Report("ready", name.isEmpty() ? location.city() : name, condition, temperature.toPlainString(),
                humidity.isNumber() && humidity.doubleValue() >= 0 && humidity.doubleValue() <= 1
                        ? Long.toString(Math.round(humidity.doubleValue() * 100)) : "",
                text(wind.path("direction"), "compass"), scale == null ? "" : scale.toString(), text(live, "validTime"),
                celsius(live.path("feelsLike")), chinaAqi(indexes));
    }

    private static String coordinate(JsonNode city, String name, int bound) throws IOException {
        String value = text(city, name);
        if (!value.matches("-?[0-9]{1,3}(\\.[0-9]{1,12})?")) {
            throw new IOException("QWeather city coordinates unavailable");
        }
        BigDecimal coordinate = new BigDecimal(value);
        if (coordinate.abs().compareTo(BigDecimal.valueOf(bound)) > 0) {
            throw new IOException("QWeather city coordinates invalid");
        }
        return coordinate.setScale(2, RoundingMode.HALF_UP).toPlainString();
    }

    private static BigDecimal celsius(JsonNode reading) {
        JsonNode value = reading.path("value");
        return "°C".equals(text(reading, "unit")) && value.isNumber()
                ? roundTemperature(value.doubleValue()) : null;
    }

    private static BigDecimal roundTemperature(double value) {
        if (!Double.isFinite(value) || value < -150 || value > 150) {
            return null;
        }
        // Round to the nearest half degree and omit the decimal for whole degrees.
        return BigDecimal.valueOf(value).multiply(BigDecimal.TWO)
                .setScale(0, RoundingMode.HALF_UP).divide(BigDecimal.TWO);
    }

    private static String amapTemperature(JsonNode live) {
        try {
            BigDecimal value = roundTemperature(Double.parseDouble(text(live, "temperature")));
            return value == null ? "" : value.toPlainString();
        } catch (NumberFormatException invalidTemperature) {
            return "";
        }
    }

    private static Integer wholeNumber(JsonNode value, int max) {
        if (!value.isNumber() || !value.canConvertToExactIntegral() || !value.canConvertToInt()) {
            return null;
        }
        int n = value.intValue();
        return n >= 0 && n <= max ? n : null;
    }

    private static Integer chinaAqi(JsonNode indexes) {
        for (JsonNode index : indexes) {
            if ("cn-mee".equals(text(index, "code"))) {
                Integer aqi = wholeNumber(index.path("aqi"), 500);
                if (aqi != null) {
                    return aqi;
                }
            }
        }
        return null;
    }

    private Report fetchAmapWeather(String key, Location location, long deadline)
            throws IOException, InterruptedException {
        JsonNode live = requestAmap("/v3/weather/weatherInfo", key,
                Map.of("city", location.code(), "extensions", "base"), deadline).path("lives").path(0);
        String condition = text(live, "weather");
        if (condition.isEmpty()) {
            throw new IOException("Weather unavailable");
        }
        String city = text(live, "city");
        return new Report("ready", city.isEmpty() ? location.city() : city, condition, amapTemperature(live),
                text(live, "humidity"), text(live, "winddirection"), text(live, "windpower"),
                text(live, "reporttime"), null, null);
    }

    private JsonNode requestAmap(String path, String key, Map<String, String> parameters, long deadline)
            throws IOException, InterruptedException {
        var query = new HashMap<>(parameters);
        query.put("key", key);
        JsonNode body = request(AMAP, path, query, Map.of(), requestTimeout(deadline));
        if (!"1".equals(body.path("status").asText())) {
            throw new IOException("Amap weather request failed");
        }
        return body;
    }

    private JsonNode requestQweather(SpeakerThirdPartyServices.QWeatherApiKeyConfiguration config,
                                     String path, Map<String, String> parameters, long deadline)
            throws IOException, InterruptedException {
        var query = new HashMap<>(parameters);
        var headers = new HashMap<>(Map.of("Accept-Encoding", "gzip"));
        switch (config.authMode()) {
            case "query" -> query.put("key", config.credential());
            case "header" -> headers.put("X-QW-Api-Key", config.credential());
            case "bearer" -> headers.put("Authorization", "Bearer " + config.credential());
            default -> throw new IOException("Unsupported QWeather authentication");
        }
        try {
            return request(config.origin().toString(), path, query, headers, requestTimeout(deadline));
        } catch (IllegalArgumentException invalidRequest) {
            throw new IOException("Invalid QWeather request");
        }
    }

    private static Duration requestTimeout(long deadline) throws IOException {
        long remaining = deadline - System.nanoTime();
        if (remaining <= 0) {
            throw new IOException("Weather request budget exhausted");
        }
        return Duration.ofNanos(Math.min(remaining, REQUEST_TIMEOUT.toNanos()));
    }

    private JsonNode request(String origin, String path, Map<String, String> parameters,
                             Map<String, String> headers, Duration timeout) throws IOException, InterruptedException {
        var builder = UriComponentsBuilder.fromUriString(origin).path(path);
        parameters.forEach((name, value) -> builder.queryParam(name, "{" + name + "}"));
        URI uri = builder.encode().buildAndExpand(parameters).toUri();
        var request = HttpRequest.newBuilder(uri).timeout(timeout).GET();
        headers.forEach(request::header);
        var response = http.send(request.build(), HttpResponse.BodyHandlers.ofByteArray());
        byte[] body = response.body();
        if (response.statusCode() != 200 || body.length > MAX_PROVIDER_BODY_BYTES) {
            throw new IOException("Weather service unavailable");
        }
        // QWeather can return gzip even when the encoding was not negotiated.
        if (body.length >= 2 && (body[0] & 0xff) == 0x1f && (body[1] & 0xff) == 0x8b) {
            try (var gzip = new GZIPInputStream(new ByteArrayInputStream(body))) {
                body = gzip.readNBytes(MAX_PROVIDER_BODY_BYTES + 1);
            }
            if (body.length > MAX_PROVIDER_BODY_BYTES) {
                throw new IOException("Weather response too large");
            }
        }
        JsonNode parsed = json.readTree(body);
        if (parsed == null || !parsed.isObject()) {
            throw new IOException("Invalid weather response");
        }
        return parsed;
    }

    static String text(JsonNode node, String name) {
        JsonNode value = node.path(name);
        if (!value.isTextual()) return "";
        String text = value.textValue().strip();
        return text.length() <= 64 && text.chars().noneMatch(Character::isISOControl) ? text : "";
    }
}
