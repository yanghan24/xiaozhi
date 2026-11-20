package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.server.LocalServerPort;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT, properties = {
        "server.servlet.context-path=/xiaozhi", "server.forward-headers-strategy=native",
        "device-control.enabled=true", "device-control.public-origin=https://speaker.example.com"
})
class DeviceEnvironmentControllerTest {
    private static final String DEVICE = "weather-http-test";
    private static final String CREDENTIAL = "weather-test-credential-0123456789abcdefghijklmnopqrstuvwxyz";
    private static final ObjectMapper JSON = new ObjectMapper();
    private static final AtomicReference<JsonNode> query = new AtomicReference<>();
    private static final AtomicReference<String> credentialHeader = new AtomicReference<>();
    private static HttpServer cache;
    private final HttpClient http = HttpClient.newHttpClient();
    @LocalServerPort int port;
    @Autowired JdbcTemplate db;

    @DynamicPropertySource
    static void cacheOrigin(DynamicPropertyRegistry registry) throws IOException {
        cache = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        cache.createContext("/internal/device-environment", exchange -> {
            try (exchange) {
                credentialHeader.set(exchange.getRequestHeaders().getFirst("Authorization"));
                var body = JSON.readTree(exchange.getRequestBody());
                query.set(body);
                int status = "99".equals(body.path("city_id").asText()) ? 503 : 200;
                byte[] data = "{\"location_status\":\"unknown\",\"city\":null}".getBytes(StandardCharsets.UTF_8);
                exchange.sendResponseHeaders(status, data.length);
                exchange.getResponseBody().write(data);
            }
        });
        cache.start();
        registry.add("device-control.weather-origin", () -> "http://127.0.0.1:" + cache.getAddress().getPort());
    }

    @AfterAll static void stopCache() { cache.stop(0); }

    @BeforeEach void register() throws Exception {
        db.update("DELETE FROM dc_device WHERE device_id=?", DEVICE);
        db.update("INSERT INTO dc_device(device_id, credential_hash) VALUES (?,?)", DEVICE,
                HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256")
                        .digest(CREDENTIAL.getBytes(StandardCharsets.UTF_8))));
        query.set(null);
    }

    private HttpResponse<String> get(String suffix, String credential) throws Exception {
        return http.send(HttpRequest.newBuilder(URI.create("http://127.0.0.1:" + port
                        + "/xiaozhi/device-control/v1/environment" + suffix))
                .header("Authorization", "Bearer " + credential).header("Device-Id", DEVICE)
                .header("X-Forwarded-Proto", "https").header("X-Forwarded-For", "8.8.8.8")
                .header("X-Real-IP", "1.1.1.1").GET().build(), HttpResponse.BodyHandlers.ofString());
    }

    @Test void requiresAnAdmittedDeviceAndRejectsCallerSuppliedAddresses() throws Exception {
        assertEquals(401, get("", CREDENTIAL + "wrong").statusCode());
        for (String invalid : new String[]{"?client_ip=8.8.8.8", "?city_id=0", "?province=Hunan",
                "?city_id=1&city=Changsha", "?city_id=1&city_id=2"}) {
            assertEquals(400, get(invalid, CREDENTIAL).statusCode(), invalid);
        }
        db.update("UPDATE dc_device SET admitted=false WHERE device_id=?", DEVICE);
        assertEquals(403, get("", CREDENTIAL).statusCode());
        assertNull(query.get());
    }

    @Test void manualLookupNeverForwardsAnAddressOrCredential() throws Exception {
        var response = get("?city=%E9%95%BF%E6%B2%99&province=Hunan", CREDENTIAL);
        assertEquals(200, response.statusCode(), response.body());
        assertEquals("no-store", response.headers().firstValue("Cache-Control").orElseThrow());
        assertEquals("unknown", JSON.readTree(response.body()).at("/data/location_status").asText());
        assertEquals(JSON.readTree("{\"city\":\"长沙\",\"province\":\"Hunan\"}"), query.get());
        assertNull(credentialHeader.get());
    }

    @Test void autoUsesServletAddressAndUpstreamErrorsStayUnavailable() throws Exception {
        assertEquals(200, get("", CREDENTIAL).statusCode());
        assertEquals(JSON.readTree("{\"client_ip\":\"8.8.8.8\"}"), query.get());
        var response = get("?city_id=99", CREDENTIAL);
        assertEquals(503, response.statusCode());
        assertEquals("WEATHER_UNAVAILABLE", JSON.readTree(response.body()).path("code").asText());
    }
}
