package xiaozhi.modules.speaker;

import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.validation.Validation;
import jakarta.validation.ValidatorFactory;
import org.junit.jupiter.api.*;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.datasource.DataSourceTransactionManager;
import org.springframework.jdbc.datasource.DriverManagerDataSource;
import org.springframework.mock.web.MockHttpServletRequest;
import xiaozhi.modules.security.user.SecurityUser;

import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.*;
import static org.junit.jupiter.api.Assumptions.assumeTrue;
import static org.mockito.Mockito.mockStatic;

class SpeakerDiagnosticsDatabaseTest {
    private JdbcTemplate db;
    private SpeakerDevices devices;
    private SpeakerDiagnostics diagnostics;
    private ValidatorFactory validators;
    private String device;
    private String other;
    private String auth;
    private String otherAuth;
    private final ObjectMapper json = new ObjectMapper();

    @BeforeEach void setup() {
        String url = System.getenv("SPRING_DATASOURCE_DRUID_URL");
        assumeTrue(url != null && url.startsWith("jdbc:mysql://172.16.18.104:13306/xiaozhi_dev?"),
                "This test requires the approved Xiaozhi development database");
        var source = new DriverManagerDataSource(url, "xiaozhi_dev", System.getenv("SPRING_DATASOURCE_DRUID_PASSWORD"));
        db = new JdbcTemplate(source);
        devices = new SpeakerDevices(db, new SpeakerSettings(true, "https://fixture.example/oidc", "", "", ""),
                new DataSourceTransactionManager(source));
        validators = Validation.buildDefaultValidatorFactory();
        diagnostics = new SpeakerDiagnostics(db, devices, json, validators.getValidator());
        device = "speaker-diag-test-" + UUID.randomUUID();
        other = "speaker-diag-test-" + UUID.randomUUID();
        auth = "Bearer " + devices.register(device, "诊断测试音箱", "p4", 0).get("credential");
        otherAuth = "Bearer " + devices.register(other, "隔离测试音箱", "p4", 0).get("credential");
    }

    @AfterEach void cleanup() {
        if (validators != null) validators.close();
        if (device == null) return;
        for (String id : List.of(device, other)) {
            db.update("DELETE FROM speaker_device_diagnostic_log WHERE device_id=?", id);
            db.update("DELETE FROM speaker_event WHERE device_id=?", id);
            db.update("DELETE FROM speaker_device_state WHERE device_id=?", id);
            db.update("DELETE FROM dc_device_presence WHERE device_id=?", id);
            db.update("DELETE FROM dc_device WHERE device_id=?", id);
        }
    }

    @Test void logsRequireOwnDeviceAndTheActiveLeaseAndAreDeDuplicated() throws Exception {
        var identity = devices.authenticate(auth, device);
        assertEquals(409, assertThrows(SpeakerFailure.class,
                () -> diagnostics.toggle(device, new SpeakerDiagnostics.Toggle(true))).status);
        assertEquals(0L, devices.heartbeat(identity, "p4", "2.2.8", true).get("diagnostics_until_ms"));
        var inactive = body(1L, "boot");
        assertEquals(409, assertThrows(SpeakerFailure.class, () -> diagnostics.upload(auth, device, inactive)).status);

        long until = enable();
        assertEquals(until, devices.heartbeat(identity, "p4", "2.2.8", true).get("diagnostics_until_ms"));
        assertEquals(401, assertThrows(SpeakerFailure.class,
                () -> diagnostics.upload(otherAuth, device, body(until, "boot"))).status);
        assertEquals(409, assertThrows(SpeakerFailure.class,
                () -> diagnostics.upload(otherAuth, other, body(until, "boot"))).status);
        assertEquals(400, assertThrows(SpeakerFailure.class,
                () -> diagnostics.upload(auth, device, body(until, "user_conversation"))).status);
        var oversized = new MockHttpServletRequest();
        oversized.setContent(new byte[8193]);
        assertEquals(413, assertThrows(SpeakerFailure.class, () -> diagnostics.upload(auth, device, oversized)).status);

        assertTrue(diagnostics.upload(auth, device, body(until, "boot")).get("accepted"));
        assertTrue(diagnostics.upload(auth, device, body(until, "boot")).get("accepted"));
        var logs = (List<?>) diagnostics.read(device).get("logs");
        assertEquals(1, logs.size());
        assertTrue(logs.getFirst().toString().contains("2.2.8"));
        assertEquals(List.of(), diagnostics.read(other).get("logs"));

        try (var security = mockStatic(SecurityUser.class)) {
            security.when(SecurityUser::getUserId).thenReturn(0L);
            assertEquals(false, diagnostics.toggle(device, new SpeakerDiagnostics.Toggle(false)).get("enabled"));
        }
        assertEquals(409, assertThrows(SpeakerFailure.class,
                () -> diagnostics.upload(auth, device, body(until, "boot"))).status);
        long next = enable();
        db.update("UPDATE speaker_device_state SET diagnostics_until_ms=? WHERE device_id=?", 1L, device);
        assertEquals(409, assertThrows(SpeakerFailure.class,
                () -> diagnostics.upload(auth, device, body(next, "boot"))).status);
    }

    private long enable() {
        try (var security = mockStatic(SecurityUser.class)) {
            security.when(SecurityUser::getUserId).thenReturn(0L);
            return (Long) diagnostics.toggle(device, new SpeakerDiagnostics.Toggle(true)).get("until_ms");
        }
    }

    private MockHttpServletRequest body(long until, String kind) throws Exception {
        var request = new MockHttpServletRequest();
        var report = Map.of("report_id", "1".repeat(32), "diagnostics_until_ms", until,
                "firmware_version", "2.2.8", "app_sha256", "a".repeat(64), "entries", List.of(Map.of(
                        "event", kind, "uptime_ms", 1800, "state", 6, "code", 4,
                        "free_heap_bytes", 180_000, "min_free_heap_bytes", 130_000)));
        request.setContent(json.writeValueAsString(report).getBytes(StandardCharsets.UTF_8));
        return request;
    }
}
