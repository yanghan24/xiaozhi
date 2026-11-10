package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.time.Clock;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.mock.mockito.MockBean;
import org.springframework.boot.test.web.server.LocalServerPort;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

/** Real Tomcat, Shiro, MySQL and Redis; run in the isolated Ubuntu test project. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT, properties = {
        "server.servlet.context-path=/xiaozhi", "server.forward-headers-strategy=native",
        "device-control.enabled=true", "device-delivery.enabled=true",
        "device-control.public-origin=https://speaker.example:18443"
})
class DeviceDeliveryHttpTest {
    private static final ObjectMapper JSON = new ObjectMapper();
    private static final long NOW = 1794182400000L;
    private static final long ADMIN = 410000000000001L;
    private static final long USER = 410000000000002L;
    private static final String ADMIN_TOKEN = "delivery-test-admin-" + "A".repeat(43);
    private static final String USER_TOKEN = "delivery-test-user-" + "B".repeat(43);
    private static final String WRITE = "a".repeat(32);
    private static final Path KEY = createKey();
    @LocalServerPort int port;
    @Autowired JdbcTemplate db;
    @MockBean(name = "deviceControlClock") Clock clock;
    private final HttpClient http = HttpClient.newHttpClient();
    private String tool;

    private static Path createKey() {
        try {
            var path = Files.createTempFile("delivery-test-key-", ".private",
                    PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rw-------")));
            Files.writeString(path, Base64.getEncoder().encodeToString(new byte[32]));
            path.toFile().deleteOnExit();
            return path;
        } catch (Exception failure) { throw new ExceptionInInitializerError(failure); }
    }

    @DynamicPropertySource
    static void configuration(DynamicPropertyRegistry properties) {
        properties.add("device-delivery.encryption-key-file", () -> KEY.toString());
    }

    @BeforeEach
    void setup() throws Exception {
        cleanup();
        when(clock.millis()).thenReturn(NOW);
        for (long operator : new long[] {ADMIN, USER}) {
            db.update("INSERT INTO sys_user(id,username,super_admin,status) VALUES (?,?,?,1)",
                    operator, "delivery-test-" + operator, operator == ADMIN ? 1 : 0);
            db.update("INSERT INTO sys_user_token(id,user_id,token,expire_date) VALUES (?,?,?,DATE_ADD(CURRENT_TIMESTAMP,INTERVAL 1 DAY))",
                    operator, operator, operator == ADMIN ? ADMIN_TOKEN : USER_TOKEN);
        }
        var response = post("authorize", ADMIN_TOKEN, null, Map.of("version", 1));
        assertEquals(200, response.statusCode(), response.body());
        tool = data(response).path("tool_token").asText();
    }

    @AfterEach
    void cleanup() {
        var devices = db.queryForList("SELECT device_id FROM dc_delivery_operation WHERE operator_id IN (?,?)", ADMIN, USER);
        db.update("DELETE FROM dc_delivery_operation WHERE operator_id IN (?,?)", ADMIN, USER);
        for (var device : devices) db.update("DELETE FROM dc_device WHERE device_id=?", device.get("device_id"));
        db.update("DELETE FROM dc_delivery_tool WHERE operator_id IN (?,?)", ADMIN, USER);
        db.update("DELETE FROM sys_user_token WHERE user_id IN (?,?)", ADMIN, USER);
        db.update("DELETE FROM sys_user WHERE id IN (?,?)", ADMIN, USER);
    }

    @Test
    void onlyCurrentAdministratorsCanAuthorizeOrUseTools() throws Exception {
        assertEquals(401, post("authorize", null, null, Map.of("version", 1)).statusCode());
        assertEquals(401, post("authorize", "invalid", null, Map.of("version", 1)).statusCode());
        assertEquals(403, post("authorize", USER_TOKEN, null, Map.of("version", 1)).statusCode());
        assertEquals(401, post("operations", ADMIN_TOKEN, id(), hardware(1)).statusCode());
        db.update("UPDATE sys_user SET super_admin=0 WHERE id=?", ADMIN);
        assertEquals(403, post("operations", tool, id(), hardware(1)).statusCode());
        db.update("UPDATE sys_user SET super_admin=1,status=0 WHERE id=?", ADMIN);
        assertEquals(403, post("operations", tool, id(), hardware(1)).statusCode());
    }

    @Test
    void toolTokenIsHashedScopedAndExpires() throws Exception {
        String stored = db.queryForObject("SELECT token_hash FROM dc_delivery_tool WHERE operator_id=?", String.class, ADMIN);
        assertEquals(DeviceDeliverySecrets.hash(tool), stored);
        assertEquals(401, post("authorize", tool, null, Map.of("version", 1)).statusCode());
        when(clock.millis()).thenReturn(NOW + 3600000);
        assertEquals(401, post("operations", tool, id(), hardware(1)).statusCode());
    }

    @Test
    void anotherOperatorCannotReadOrResumeAnExistingOperation() throws Exception {
        var a = create(1, id());
        db.update("UPDATE sys_user SET super_admin=1 WHERE id=?", USER);
        var authorization = post("authorize", USER_TOKEN, null, Map.of("version", 1));
        assertEquals(200, authorization.statusCode(), authorization.body());
        String other = data(authorization).path("tool_token").asText();
        assertEquals(404, send("GET", "/device-delivery/v1/operations/" + a.path("operation_id").asText(), other, null, null, null, true).statusCode());
        assertEquals(409, post("operations", other, a.path("operation_id").asText(), hardware(1)).statusCode());
        assertEquals(404, post("operations/" + a.path("operation_id").asText() + "/complete", other, null,
                Map.of("version", 1, "write_operation_id", WRITE)).statusCode());
    }

    @Test
    void twoDevicesHaveDistinctPendingIdentitiesAndCannotUseEachOthersCredentials() throws Exception {
        var a = create(1, id());
        var b = create(2, id());
        assertNotEquals(a.path("device_id"), b.path("device_id"));
        assertNotEquals(a.path("identity").path("credential"), b.path("identity").path("credential"));
        for (var device : List.of(a, b)) {
            assertEquals("pending", device.path("status").asText());
            var manifest = device.path("identity");
            assertEquals(6, manifest.size());
            assertEquals("https://speaker.example:18443/xiaozhi/ota/", manifest.path("ota_url").asText());
            var state = send("GET", "/device-control/v1/state", credential(device), device.path("device_id").asText(), null, null, true);
            assertEquals(403, state.statusCode());
            assertFalse(state.body().contains("verification_uri"));
        }
        assertEquals(401, check(a, credential(b), hardware(1)).statusCode());
        assertEquals(403, check(a, credential(a), hardware(2)).statusCode());
        assertEquals(200, check(b, credential(b), hardware(2)).statusCode());
        assertTrue(get(a).path("device_verified_at_ms").isNull());
    }

    @Test
    void interruptedCreateReplaysSameIdentityButHardwareCannotCreateAnotherRecord() throws Exception {
        String operation = id();
        var a = create(1, operation);
        assertEquals(a, data(post("operations", tool, operation, hardware(1))));
        assertEquals(409, post("operations", tool, operation, hardware(2)).statusCode());
        assertEquals(409, post("operations", tool, id(), hardware(1)).statusCode());
        assertEquals(1, db.queryForObject("SELECT COUNT(*) FROM dc_delivery_operation WHERE operator_id=?", Integer.class, ADMIN));
        assertEquals(1, db.queryForObject("SELECT COUNT(*) FROM dc_device WHERE device_id=?", Integer.class, a.path("device_id").asText()));
        assertFalse(get(a).has("identity"));
        assertFalse(get(a).toString().contains(credential(a)));
        String encrypted = db.queryForObject("SELECT credential_cipher FROM dc_delivery_operation WHERE operation_id=?", String.class, operation);
        assertFalse(encrypted.contains(credential(a)));
    }

    @Test
    void concurrentCreateHasOneDurableOperationAndNoOrphanDevice() throws Exception {
        String operation = id();
        long before = db.queryForObject("SELECT COUNT(*) FROM dc_device", Long.class);
        var tasks = java.util.stream.IntStream.range(0, 6).mapToObj(i -> CompletableFuture.supplyAsync(() -> {
            try { return post("operations", tool, operation, hardware(1)); }
            catch (Exception failure) { throw new RuntimeException(failure); }
        })).toList();
        CompletableFuture.allOf(tasks.toArray(CompletableFuture[]::new)).get(30, java.util.concurrent.TimeUnit.SECONDS);
        var first = data(tasks.getFirst().join());
        for (var task : tasks) {
            assertEquals(200, task.join().statusCode(), task.join().body());
            assertEquals(first, data(task.join()));
        }
        assertEquals(before + 1, db.queryForObject("SELECT COUNT(*) FROM dc_device", Long.class));
    }

    @Test
    void completionRequiresBothReceiptsAndDoesNotBindOrGrantVoicePermission() throws Exception {
        var a = create(1, id());
        assertEquals(409, complete(a, WRITE).statusCode());
        var checked = check(a, credential(a), hardware(1));
        assertEquals(200, checked.statusCode());
        assertEquals("pending", data(checked).path("status").asText());
        var done = complete(a, WRITE);
        assertEquals(200, done.statusCode(), done.body());
        assertEquals("delivered", data(done).path("status").asText());
        assertEquals(data(done), data(complete(a, WRITE)));
        assertEquals(409, complete(a, "b".repeat(32)).statusCode());
        var row = db.queryForMap("SELECT * FROM dc_device WHERE device_id=?", a.path("device_id").asText());
        assertNull(row.get("owner_subject"));
        assertEquals(2, ((Number) row.get("revision")).longValue());
        assertEquals(2, ((Number) row.get("permission_version")).longValue());
        var state = send("GET", "/device-control/v1/state", credential(a), a.path("device_id").asText(), null, null, true);
        assertEquals(200, state.statusCode(), state.body());
        assertEquals("blocked", data(state).path("use_permission").asText());
        assertTrue(data(state).path("voice").isNull());
        var delivered = check(a, credential(a), hardware(1));
        assertEquals("delivered", data(delivered).path("status").asText());
        var artifacts = Files.createDirectories(Path.of("target", "device-delivery-compatibility"));
        Files.writeString(artifacts.resolve("pending.json"), checked.body());
        Files.writeString(artifacts.resolve("delivered.json"), delivered.body());
    }

    @Test
    void completionNeverReenablesDisabledDeviceOrChangesAnOwner() throws Exception {
        var a = create(1, id());
        assertEquals(200, check(a, credential(a), hardware(1)).statusCode());
        db.update("UPDATE dc_device SET owner_issuer='https://issuer.example',owner_subject='delivery-test-owner',revision=revision+1 WHERE device_id=?", a.path("device_id").asText());
        assertEquals(409, complete(a, WRITE).statusCode());
        assertEquals("delivery-test-owner", db.queryForObject("SELECT owner_subject FROM dc_device WHERE device_id=?", String.class, a.path("device_id").asText()));
        var b = create(2, id());
        assertEquals(200, check(b, credential(b), hardware(2)).statusCode());
        assertEquals(200, complete(b, WRITE).statusCode());
        db.update("UPDATE dc_device SET admitted=FALSE,revision=revision+1 WHERE device_id=?", b.path("device_id").asText());
        assertEquals(409, complete(b, WRITE).statusCode());
        assertEquals(403, check(b, credential(b), hardware(2)).statusCode());
    }

    @Test
    void rotationOrAdministrativeChangesFencePendingOperations() throws Exception {
        var a = create(1, id());
        assertEquals(200, check(a, credential(a), hardware(1)).statusCode());
        db.update("UPDATE dc_device SET credential_hash=? WHERE device_id=?", DeviceDeliverySecrets.hash("Z".repeat(43)), a.path("device_id").asText());
        assertEquals(409, post("operations", tool, a.path("operation_id").asText(), hardware(1)).statusCode());
        assertEquals(409, complete(a, WRITE).statusCode());
        assertEquals(401, check(a, credential(a), hardware(1)).statusCode());
        var b = create(2, id());
        db.update("UPDATE dc_device SET revision=revision+1 WHERE device_id=?", b.path("device_id").asText());
        assertEquals(409, complete(b, WRITE).statusCode());
    }

    @Test
    void malformedBodiesAndInsecureTransportNeverIssueCredentials() throws Exception {
        for (String body : List.of("{\"version\":18446744073709551617}", "{\"version\":1,\"version\":1}",
                "{\"version\":1.0}", "{\"version\":true}", "{\"version\":1,\"extra\":0}", "{\"version\":1} {}")) {
            assertEquals(400, send("POST", "/device-delivery/v1/authorize", ADMIN_TOKEN, null, null, body, true).statusCode());
        }
        assertEquals(403, send("POST", "/device-delivery/v1/authorize", ADMIN_TOKEN, null, null, "{\"version\":1}", false).statusCode());
        assertEquals(400, post("operations?unused=1", tool, id(), hardware(1)).statusCode());
        var tooLarge = send("POST", "/device-delivery/v1/operations", tool, null, id(), " ".repeat(4097), true);
        assertEquals(400, tooLarge.statusCode());
        assertEquals("no-store", tooLarge.headers().firstValue("Cache-Control").orElse(""));
    }

    @Test
    void encryptedCredentialCannotBeMovedBetweenOperations() throws Exception {
        var a = create(1, id());
        var b = create(2, id());
        String cipher = db.queryForObject("SELECT credential_cipher FROM dc_delivery_operation WHERE operation_id=?", String.class, a.path("operation_id").asText());
        db.update("UPDATE dc_delivery_operation SET credential_cipher=? WHERE operation_id=?", cipher, b.path("operation_id").asText());
        assertEquals(503, post("operations", tool, b.path("operation_id").asText(), hardware(2)).statusCode());
    }

    private static String id() { return UUID.randomUUID().toString().replace("-", ""); }
    private static Map<String, Object> hardware(int n) {
        return Map.of("version", 1, "hardware_id", "02000000000" + n,
                "network_hardware_id", "02000000010" + n, "board_type", "test-p4");
    }
    private JsonNode create(int hardware, String operation) throws Exception {
        var response = post("operations", tool, operation, hardware(hardware));
        assertEquals(200, response.statusCode(), response.body());
        assertEquals("no-store", response.headers().firstValue("Cache-Control").orElse(""));
        return data(response);
    }
    private static String credential(JsonNode device) { return device.path("identity").path("credential").asText(); }
    private static JsonNode data(HttpResponse<String> response) throws Exception { return JSON.readTree(response.body()).path("data"); }
    private JsonNode get(JsonNode device) throws Exception {
        var response = send("GET", "/device-delivery/v1/operations/" + device.path("operation_id").asText(), tool, null, null, null, true);
        assertEquals(200, response.statusCode(), response.body());
        return data(response);
    }
    private HttpResponse<String> complete(JsonNode device, String write) throws Exception {
        return post("operations/" + device.path("operation_id").asText() + "/complete", tool, null,
                Map.of("version", 1, "write_operation_id", write));
    }
    private HttpResponse<String> check(JsonNode device, String credential, Map<String, Object> hardware) throws Exception {
        return send("POST", "/device-delivery/v1/device-check", credential, device.path("device_id").asText(), null,
                JSON.writeValueAsString(Map.of("version", 1, "hardware_id", hardware.get("hardware_id"),
                        "network_hardware_id", hardware.get("network_hardware_id"))), true);
    }
    private HttpResponse<String> post(String suffix, String token, String key, Object body) throws Exception {
        return send("POST", "/device-delivery/v1/" + suffix, token, null, key, JSON.writeValueAsString(body), true);
    }
    private HttpResponse<String> send(String method, String suffix, String token, String device, String key, String body, boolean secure) throws Exception {
        var builder = HttpRequest.newBuilder(URI.create("http://localhost:" + port + "/xiaozhi" + suffix))
                .timeout(java.time.Duration.ofSeconds(20));
        if (secure) builder.header("X-Forwarded-Proto", "https");
        if (token != null) builder.header("Authorization", "Bearer " + token);
        if (device != null) builder.header("Device-Id", device);
        if (key != null) builder.header("Idempotency-Key", key);
        if (body != null) builder.header("Content-Type", "application/json");
        return http.send(builder.method(method, body == null ? HttpRequest.BodyPublishers.noBody() : HttpRequest.BodyPublishers.ofString(body)).build(), HttpResponse.BodyHandlers.ofString());
    }
}
