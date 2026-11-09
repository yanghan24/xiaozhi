package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.*;

import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeEach;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.jdbc.core.JdbcTemplate;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.time.Clock;
import org.springframework.boot.test.mock.mockito.MockBean;
import static org.mockito.Mockito.when;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.ArrayList;
import java.util.HashSet;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.boot.web.servlet.context.ServletWebServerApplicationContext;
import org.springframework.boot.context.logging.LoggingApplicationListener;
import xiaozhi.AdminApplication;
import java.io.ByteArrayInputStream;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.Socket;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.server.LocalServerPort;

import com.fasterxml.jackson.databind.ObjectMapper;

/** Exercises the real servlet and Shiro chain, not a standalone controller. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT, properties = {
        "server.servlet.context-path=/xiaozhi",
        "server.forward-headers-strategy=native",
        "device-control.enabled=true", "device-control.public-origin=https://speaker.example.com:8443"
})
class DeviceControlHttpTest {
    @LocalServerPort int port;
    private final HttpClient http = HttpClient.newHttpClient();
    private final ObjectMapper json = new ObjectMapper();
    @Autowired JdbcTemplate db;
    @MockBean(name = "deviceControlClock") Clock clock;
    private static final long NOW = 1794182400000L;
    private static final String DEVICE = "control-http-test-a";
    private static final String CREDENTIAL = "test-only-0123456789abcdefghijklmnopqrstuvwxyzABCDEFG";

    @BeforeEach
    void preregisterDevice() throws Exception {
        when(clock.millis()).thenReturn(NOW);
        db.update("DELETE FROM dc_state_ack WHERE device_id=?", DEVICE);
        db.update("DELETE FROM dc_request_operation WHERE device_id=?", DEVICE);
        db.update("DELETE FROM dc_request WHERE device_id=?", DEVICE);
        db.update("DELETE FROM dc_device WHERE device_id=?", DEVICE);
        db.update("INSERT INTO dc_device(device_id, credential_hash) VALUES (?,?)", DEVICE,
                HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256")
                        .digest(CREDENTIAL.getBytes(StandardCharsets.UTF_8))));
    }

    @Test
    void creatingAndRetryingBindingRequestPreservesItsIdentityAndDeadline() throws Exception {
        var first = post("requests", "request-one", "{\"intent\":\"auto\",\"expected_permission_version\":1}");
        assertEquals(200, first.statusCode(), first.body());
        var data = json.readTree(first.body()).path("data");
        var binding = data.path("binding_request");
        assertEquals("bind", binding.path("purpose").asText());
        assertEquals(NOW + 600000, binding.path("expires_at_ms").asLong());
        assertEquals(600000, binding.path("expires_in_ms").asLong());
        assertTrue(binding.path("verification_uri").asText().startsWith("https://speaker.example.com:8443/xiaozhi/bind/"));
        assertEquals(2, data.path("revision").asLong());
        when(clock.millis()).thenReturn(NOW + 1000);
        var retry = json.readTree(post("requests", "request-one", "{\"intent\":\"auto\",\"expected_permission_version\":1}").body()).path("data");
        assertEquals(binding.path("request_id"), retry.path("binding_request").path("request_id"));
        assertEquals(NOW + 600000, retry.path("binding_request").path("expires_at_ms").asLong());
        assertEquals(599000, retry.path("binding_request").path("expires_in_ms").asLong());
        var snapshot = json.readTree(getState(DEVICE, CREDENTIAL).body()).path("data");
        assertEquals(retry.path("binding_request"), snapshot.path("binding_request"));
        assertEquals(1, snapshot.path("permission_version").asLong());
    }

    private HttpResponse<String> post(String path, String key, String body) throws Exception {
        var request = HttpRequest.newBuilder(URI.create("http://localhost:" + port + "/xiaozhi/device-control/v1/" + path))
                .header("Device-Id", DEVICE).header("Authorization", "Bearer " + CREDENTIAL)
                .header("X-Forwarded-Proto", "https").header("Content-Type", "application/json");
        if (key != null) request.header("Idempotency-Key", key);
        return http.send(request.POST(HttpRequest.BodyPublishers.ofString(body)).build(), HttpResponse.BodyHandlers.ofString());
    }

    @Test
    void expiredRequestsCannotBeResurrectedByIdempotentRetry() throws Exception {
        var first = json.readTree(post("requests", "expiry", "{\"intent\":\"auto\",\"expected_permission_version\":1}").body());
        when(clock.millis()).thenReturn(NOW + 600000);
        var expired = post("requests", "expiry", "{\"intent\":\"auto\",\"expected_permission_version\":1}");
        assertEquals(410, expired.statusCode(), expired.body());
        assertEquals("REQUEST_EXPIRED", json.readTree(expired.body()).path("code").asText());
        var state = json.readTree(getState(DEVICE, CREDENTIAL).body()).path("data");
        assertTrue(state.path("binding_request").isNull());
        assertEquals(3, state.path("revision").asLong());
        var fresh = json.readTree(post("requests", "new-operation", "{\"intent\":\"auto\",\"expected_permission_version\":1}").body());
        assertNotEquals(first.path("data").path("binding_request").path("request_id"),
                fresh.path("data").path("binding_request").path("request_id"));
    }

    @Test
    void acknowledgementRequiresCurrentStoppedStateAndNeverGrantsPermission() throws Exception {
        String ack = "{\"revision\":1,\"permission_version\":1,\"audio_stopped\":true}";
        var response = post("state-acks", null, ack);
        assertEquals(200, response.statusCode(), response.body());
        assertTrue(json.readTree(response.body()).path("data").path("accepted").asBoolean());
        assertEquals(response.body(), post("state-acks", null, ack).body());
        assertEquals(400, post("state-acks", null,
                "{\"revision\":1,\"permission_version\":1,\"audio_stopped\":false}").statusCode());
        post("requests", "advance-revision", "{\"intent\":\"auto\",\"expected_permission_version\":1}");
        assertEquals(409, post("state-acks", null, ack).statusCode());
        var state = json.readTree(getState(DEVICE, CREDENTIAL).body()).path("data");
        assertEquals("blocked", state.path("use_permission").asText());
        assertEquals(1, state.path("permission_version").asLong());
        assertTrue(state.path("lease").isNull());
    }

    @Test
    void preregisteredDeviceReadsBlockedStateButCannotImpersonateAnotherDevice() throws Exception {
        var response = getState(DEVICE, CREDENTIAL);
        assertEquals(200, response.statusCode(), response.body());
        var state = json.readTree(response.body()).path("data");
        assertEquals(DEVICE, state.path("device_id").asText());
        assertEquals("unbound", state.path("binding_state").asText());
        assertEquals("blocked", state.path("use_permission").asText());
        assertTrue(state.path("voice").isNull());
        assertTrue(state.path("lease").isNull());
        assertEquals(401, getState("another-speaker", CREDENTIAL).statusCode());
        assertEquals(401, getState(DEVICE, CREDENTIAL + "wrong").statusCode());
    }

    private HttpResponse<String> getState(String device, String credential) throws Exception {
        return http.send(HttpRequest.newBuilder(
                URI.create("http://localhost:" + port + "/xiaozhi/device-control/v1/state"))
                .header("Device-Id", device).header("Authorization", "Bearer " + credential)
                .header("X-Forwarded-Proto", "https")
                .GET().build(), HttpResponse.BodyHandlers.ofString());
    }

    @Test
    void longPollWaitsForARevisionChangeThenReturnsTheCommittedRequest() throws Exception {
        var waiting = http.sendAsync(HttpRequest.newBuilder(
                URI.create("http://localhost:" + port + "/xiaozhi/device-control/v1/state?after_revision=1&wait_ms=2000"))
                .timeout(Duration.ofSeconds(5)).header("Device-Id", DEVICE)
                .header("Authorization", "Bearer " + CREDENTIAL).header("X-Forwarded-Proto", "https")
                .GET().build(), HttpResponse.BodyHandlers.ofString());
        Thread.sleep(300);
        assertFalse(waiting.isDone(), "Unchanged state must be held for the requested wait");
        var created = json.readTree(post("requests", "wake-poll", "{\"intent\":\"auto\",\"expected_permission_version\":1}").body()).path("data");
        var state = json.readTree(waiting.get(2, TimeUnit.SECONDS).body()).path("data");
        assertEquals(created.path("revision"), state.path("revision"));
        assertEquals(created.path("binding_request"), state.path("binding_request"));
    }

    @Test
    void protocolRejectsAmbiguousBodiesQueryCredentialsAndUnsupportedVersions() throws Exception {
        for (String body : new String[] {
                "{\"intent\":\"auto\",\"expected_permission_version\":\"1\"}",
                "{\"intent\":\"auto\",\"expected_permission_version\":1.0}",
                "{\"intent\":\"auto\",\"expected_permission_version\":1,\"extra\":true}",
                "{\"intent\":\"account\",\"intent\":\"auto\",\"expected_permission_version\":1}",
                "{\"intent\":\"auto\",\"expected_permission_version\":1} {}"
        }) {
            assertEquals(400, post("requests", "invalid-body", body).statusCode(), body);
        }
        for (String path : new String[] {"state?wait_ms=20001", "state?after_revision=-1",
                "state?wait_ms=0&wait_ms=1", "state?authorization=secret", "state?extra=1", "../v2/state"}) {
            assertEquals(400, getPath(port, path).statusCode(), path);
        }
    }

    private HttpResponse<String> getPath(int serverPort, String path) throws Exception {
        return http.send(HttpRequest.newBuilder(
                URI.create("http://localhost:" + serverPort + "/xiaozhi/device-control/v1/" + path).normalize())
                .timeout(Duration.ofSeconds(5)).header("Device-Id", DEVICE)
                .header("Authorization", "Bearer " + CREDENTIAL).header("X-Forwarded-Proto", "https")
                .GET().build(), HttpResponse.BodyHandlers.ofString());
    }

    @Test
    void independentInstancesShareRequestsAndObserveChangesAfterRestart() throws Exception {
        String body = "{\"intent\":\"auto\",\"expected_permission_version\":1}";
        String requestId;
        try (var second = anotherInstance()) {
            int secondPort = second.getWebServer().getPort();
            var waiting = http.sendAsync(HttpRequest.newBuilder(
                    URI.create("http://localhost:" + secondPort + "/xiaozhi/device-control/v1/state?after_revision=1&wait_ms=2000"))
                    .header("Device-Id", DEVICE).header("Authorization", "Bearer " + CREDENTIAL)
                    .header("X-Forwarded-Proto", "https").GET().build(), HttpResponse.BodyHandlers.ofString());
            Thread.sleep(300);
            assertFalse(waiting.isDone());
            var pending = new ArrayList<CompletableFuture<HttpResponse<String>>>();
            for (int i = 0; i < 8; i++) {
                int target = i % 2 == 0 ? port : secondPort;
                pending.add(http.sendAsync(HttpRequest.newBuilder(URI.create("http://localhost:" + target + "/xiaozhi/device-control/v1/requests"))
                        .header("Device-Id", DEVICE).header("Authorization", "Bearer " + CREDENTIAL)
                        .header("X-Forwarded-Proto", "https").header("Content-Type", "application/json")
                        .header("Idempotency-Key", "concurrent-" + i)
                        .POST(HttpRequest.BodyPublishers.ofString(body)).build(), HttpResponse.BodyHandlers.ofString()));
            }
            var ids = new HashSet<String>();
            for (var future : pending) {
                var response = future.get(5, TimeUnit.SECONDS);
                assertEquals(200, response.statusCode(), response.body());
                var data = json.readTree(response.body()).path("data");
                assertEquals(2, data.path("revision").asLong());
                ids.add(data.path("binding_request").path("request_id").asText());
            }
            assertEquals(1, ids.size());
            requestId = ids.iterator().next();
            assertEquals(requestId, json.readTree(waiting.get(2, TimeUnit.SECONDS).body())
                    .path("data").path("binding_request").path("request_id").asText());
        }
        try (var restarted = anotherInstance()) {
            var response = getPath(restarted.getWebServer().getPort(), "state");
            assertEquals(200, response.statusCode(), response.body());
            assertEquals(requestId, json.readTree(response.body()).path("data").path("binding_request").path("request_id").asText());
            assertEquals("blocked", json.readTree(response.body()).path("data").path("use_permission").asText());
        }
        assertEquals(requestId, json.readTree(post("requests", "concurrent-0", body).body())
                .path("data").path("binding_request").path("request_id").asText());
    }

    private ServletWebServerApplicationContext anotherInstance() {
        var application = new SpringApplicationBuilder(AdminApplication.class)
                .initializers(context -> context.getBeanFactory().registerSingleton("deviceControlClock", clock))
                .application();
        // Both servers share this test JVM, so the secondary must not own or shut down global logging.
        application.setListeners(application.getListeners().stream()
                .filter(listener -> !(listener instanceof LoggingApplicationListener)).toList());
        application.setRegisterShutdownHook(false);
        return (ServletWebServerApplicationContext) application.run(
                        "--server.port=0", "--server.servlet.context-path=/xiaozhi", "--server.forward-headers-strategy=native",
                        "--device-control.enabled=true", "--device-control.public-origin=https://speaker.example.com:8443");
    }

    @Test
    void conflictsRevocationAndDisabledDevicesCannotReuseRequests() throws Exception {
        String body = "{\"intent\":\"auto\",\"expected_permission_version\":1}";
        assertEquals(200, post("requests", "conflict", body).statusCode());
        var conflict = post("requests", "conflict", "{\"intent\":\"account\",\"expected_permission_version\":1}");
        assertEquals(409, conflict.statusCode());
        assertEquals("IDEMPOTENCY_CONFLICT", json.readTree(conflict.body()).path("code").asText());
        assertEquals(409, post("requests", "stale", "{\"intent\":\"auto\",\"expected_permission_version\":0}").statusCode());
        db.update("UPDATE dc_device SET reason='device_disabled',revision=revision+1,permission_version=permission_version+1 WHERE device_id=?", DEVICE);
        var disabled = json.readTree(getState(DEVICE, CREDENTIAL).body()).path("data");
        assertEquals("device_disabled", disabled.path("reason").asText());
        assertTrue(disabled.path("binding_request").isNull());
        assertEquals(403, post("requests", "disabled", "{\"intent\":\"auto\",\"expected_permission_version\":2}").statusCode());
        db.update("UPDATE dc_device SET credential_hash=NULL WHERE device_id=?", DEVICE);
        assertEquals(401, getState(DEVICE, CREDENTIAL).statusCode());
        assertEquals(401, post("requests", "conflict", body).statusCode());
    }

    @Test
    void boundBlockedDeviceCanSwitchBetweenResumeAndAccountWithoutRevivingOldRequests() throws Exception {
        db.update("UPDATE dc_device SET owner_issuer='https://issuer.example',owner_subject='fixture-owner',reason='user_signed_out' WHERE device_id=?", DEVICE);
        var resume = json.readTree(post("requests", "resume", "{\"intent\":\"auto\",\"expected_permission_version\":1}").body()).path("data");
        assertEquals("resume", resume.path("binding_request").path("purpose").asText());
        var account = json.readTree(post("requests", "account", "{\"intent\":\"account\",\"expected_permission_version\":1}").body()).path("data");
        assertEquals("account", account.path("binding_request").path("purpose").asText());
        assertNotEquals(resume.path("binding_request").path("request_id"), account.path("binding_request").path("request_id"));
        assertEquals(409, post("requests", "resume", "{\"intent\":\"auto\",\"expected_permission_version\":1}").statusCode());
        assertEquals(1, account.path("permission_version").asLong());
    }

    @Test
    void unsupportedContentTypesAndChunkedOversizedBodiesUseProtocolErrors() throws Exception {
        for (String contentType : new String[] {"", "text/plain", "application/json"}) {
            var request = HttpRequest.newBuilder(URI.create("http://localhost:" + port + "/xiaozhi/device-control/v1/requests"))
                    .header("Device-Id", DEVICE).header("Authorization", "Bearer " + CREDENTIAL)
                    .header("X-Forwarded-Proto", "https").header("Idempotency-Key", "body-limit");
            if (!contentType.isEmpty()) request.header("Content-Type", contentType);
            var publisher = contentType.equals("application/json")
                    ? HttpRequest.BodyPublishers.ofInputStream(() -> new ByteArrayInputStream(new byte[8192]))
                    : HttpRequest.BodyPublishers.ofString("{\"intent\":\"auto\",\"expected_permission_version\":1}");
            var response = http.send(request.POST(publisher).build(), HttpResponse.BodyHandlers.ofString());
            assertEquals(400, response.statusCode(), "Content-Type=" + contentType + " " + response.body());
            assertEquals("INVALID_REQUEST", json.readTree(response.body()).path("code").asText());
            assertEquals("no-store", response.headers().firstValue("Cache-Control").orElse(""));
        }
    }

    @Test
    void oversizedChunkedUploadIsRejectedBeforeTheSenderFinishes() throws Exception {
        try (var socket = new Socket("localhost", port)) {
            socket.setSoTimeout(3000);
            var output = socket.getOutputStream();
            String headers = "POST /xiaozhi/device-control/v1/requests HTTP/1.1\r\nHost: localhost\r\n"
                    + "Device-Id: " + DEVICE + "\r\nAuthorization: Bearer " + CREDENTIAL + "\r\n"
                    + "X-Forwarded-Proto: https\r\nContent-Type: application/json\r\n"
                    + "Idempotency-Key: chunked-incomplete\r\nTransfer-Encoding: chunked\r\n\r\n2000\r\n";
            output.write(headers.getBytes(StandardCharsets.US_ASCII));
            output.write(" ".repeat(8192).getBytes(StandardCharsets.US_ASCII));
            output.write("\r\n".getBytes(StandardCharsets.US_ASCII));
            output.flush();
            // No final chunk: an unbounded converter would wait here for the rest of the body.
            String status = new BufferedReader(new InputStreamReader(socket.getInputStream(), StandardCharsets.US_ASCII)).readLine();
            assertNotNull(status);
            assertTrue(status.contains(" 400 "), status);
        }
    }

    @Test
    void selfReportedDeviceIdentityCannotReadControlState() throws Exception {
        var response = http.send(HttpRequest.newBuilder(
                URI.create("http://localhost:" + port + "/xiaozhi/device-control/v1/state"))
                .header("Device-Id", "unregistered-speaker").GET().build(), HttpResponse.BodyHandlers.ofString());
        assertEquals(401, response.statusCode(), response.body());
        assertEquals("INVALID_DEVICE_CREDENTIAL", json.readTree(response.body()).path("code").asText());
        assertEquals("no-store", response.headers().firstValue("Cache-Control").orElse(""));
    }
}
