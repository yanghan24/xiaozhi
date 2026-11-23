package xiaozhi.modules.speaker;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import java.io.IOException;
import java.math.RoundingMode;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.security.GeneralSecurityException;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.zip.GZIPInputStream;
import org.springframework.web.util.UriComponentsBuilder;

final class QWeatherClient {
    record Day(String date, String condition, String minimum, String maximum) {}
    private record Place(String name, String coordinates) {}
    private record Lookup(long version, String city) {}

    private final ObjectMapper json;
    private final HttpClient http;
    private final Cache<Lookup, SpeakerWeather.Report> current = CacheBuilder.newBuilder()
            .maximumSize(1024).expireAfterWrite(Duration.ofMinutes(20)).build();
    private final Cache<Lookup, List<Day>> forecast = CacheBuilder.newBuilder()
            .maximumSize(1024).expireAfterWrite(Duration.ofHours(1)).build();

    QWeatherClient(ObjectMapper json, HttpClient http) {
        this.json = json;
        this.http = http;
    }

    SpeakerWeather.Report current(SpeakerThirdPartyServices.QWeatherConfiguration config, String city)
            throws java.util.concurrent.ExecutionException {
        return current.get(new Lookup(config.version(), city), () -> {
            Place place = locate(config, city);
            JsonNode live = request(config, "/weather/v1/current/" + place.coordinates(),
                    Map.of("lang", "zh", "localTime", "true"));
            String condition = SpeakerWeather.text(live.path("condition"), "text");
            String temperature = number(live.path("temperature").path("value"));
            if (condition.isEmpty() || temperature.isEmpty()) throw new IOException("Weather unavailable");
            JsonNode wind = live.path("wind");
            JsonNode humidity = live.path("humidity");
            return new SpeakerWeather.Report("ready", place.name(), condition, temperature,
                    humidity.isNumber() ? Long.toString(Math.round(humidity.asDouble() * 100)) : "",
                    SpeakerWeather.text(wind.path("direction"), "compass"), number(wind.path("scale")),
                    SpeakerWeather.text(live, "validTime"));
        });
    }

    List<Day> forecast(SpeakerThirdPartyServices.QWeatherConfiguration config, String city)
            throws java.util.concurrent.ExecutionException {
        return forecast.get(new Lookup(config.version(), city), () -> {
            Place place = locate(config, city);
            JsonNode result = request(config, "/weather/v1/daily/" + place.coordinates(),
                    Map.of("lang", "zh", "localTime", "true", "days", "7"));
            List<Day> days = new ArrayList<>();
            for (JsonNode day : result.path("days")) {
                String time = SpeakerWeather.text(day.path("daytime"), "forecastStartTime");
                String condition = SpeakerWeather.text(day.path("daytime").path("condition"), "text");
                String minimum = number(day.path("temperatureMin").path("value"));
                String maximum = number(day.path("temperatureMax").path("value"));
                if (time.length() >= 10 && !condition.isEmpty() && !minimum.isEmpty() && !maximum.isEmpty()) {
                    days.add(new Day(time.substring(0, 10), condition, minimum, maximum));
                }
            }
            return List.copyOf(days);
        });
    }

    private Place locate(SpeakerThirdPartyServices.QWeatherConfiguration config, String city)
            throws IOException, InterruptedException, GeneralSecurityException {
        JsonNode body = request(config, "/geo/v2/city/lookup", Map.of("location", city, "lang", "zh", "number", "1"));
        JsonNode match = body.path("location").path(0);
        if (!"200".equals(body.path("code").asText()) || match.isMissingNode()) throw new IOException("City unavailable");
        String name = SpeakerWeather.text(match, "name");
        String latitude = coordinate(SpeakerWeather.text(match, "lat"), 90);
        String longitude = coordinate(SpeakerWeather.text(match, "lon"), 180);
        if (name.isEmpty() || latitude.isEmpty() || longitude.isEmpty()) throw new IOException("City unavailable");
        return new Place(name, latitude + "/" + longitude);
    }

    private JsonNode request(SpeakerThirdPartyServices.QWeatherConfiguration config, String path, Map<String, String> parameters)
            throws IOException, InterruptedException, GeneralSecurityException {
        var builder = UriComponentsBuilder.fromUriString(config.url().replaceFirst("/$", "")).path(path);
        parameters.forEach((name, value) -> builder.queryParam(name, "{" + name + "}"));
        var uri = builder.encode().buildAndExpand(parameters).toUri();
        var request = HttpRequest.newBuilder(uri).timeout(Duration.ofSeconds(3))
                .header("Authorization", "Bearer " + QWeatherJwt.sign(json, config, Instant.now()))
                .header("Accept-Encoding", "gzip").GET().build();
        var response = http.send(request, HttpResponse.BodyHandlers.ofByteArray());
        if (response.statusCode() != 200 || response.body().length > 65536)
            throw new IOException("Weather service unavailable");
        try (var source = new java.io.ByteArrayInputStream(response.body())) {
            var input = "gzip".equalsIgnoreCase(response.headers().firstValue("Content-Encoding").orElse(""))
                    ? new GZIPInputStream(source) : source;
            byte[] bytes;
            try (input) {
                bytes = input.readNBytes(65537);
            }
            if (bytes.length > 65536) throw new IOException("Weather response too large");
            JsonNode body = json.readTree(bytes);
            if (body == null || !body.isObject()) throw new IOException("Invalid weather response");
            return body;
        }
    }

    private static String number(JsonNode value) {
        return value.isNumber() ? value.decimalValue().setScale(0, RoundingMode.HALF_UP).toPlainString() : "";
    }

    private static String coordinate(String value, int bound) {
        if (!value.matches("-?[0-9]{1,3}(\\.[0-9]{1,12})?")) return "";
        var degrees = new java.math.BigDecimal(value);
        return degrees.abs().compareTo(java.math.BigDecimal.valueOf(bound)) > 0 ? ""
                : degrees.setScale(2, RoundingMode.HALF_UP).toPlainString();
    }
}
