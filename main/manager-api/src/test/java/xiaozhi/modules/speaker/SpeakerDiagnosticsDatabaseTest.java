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

    @Test void voiceTimingRequiresBoundedMetadataAndTheSameLease() throws Exception {
        devices.heartbeat(devices.authenticate(auth, device), "p4", "2.6.7", true);
        long until = enable();
        var entry = Map.of("event", "voice_timing", "uptime_ms", 1800,
                "phase", "first_output", "trace_id", "a".repeat(32), "elapsed_ms", 900);
        assertTrue(diagnostics.upload(auth, device, timingBody(until, entry)).get("accepted"));
        var logs = (List<?>) diagnostics.read(device).get("logs");
        assertEquals(1, logs.size());
        assertTrue(logs.getFirst().toString().contains("first_output"));
        assertEquals(409, assertThrows(SpeakerFailure.class,
                () -> diagnostics.upload(auth, device, timingBody(until + 1, entry))).status);
        assertEquals(400, assertThrows(SpeakerFailure.class,
                () -> diagnostics.upload(auth, device, timingBody(until,
                        Map.of("event", "voice_timing", "phase", "first_output")))).status);
    }

    @Test void displayDiagnosticsRetainCountsAndRejectIncompleteOrInvalidSamples() throws Exception {
        devices.heartbeat(devices.authenticate(auth, device), "p4", "2.6.10", true);
        long until = enable();
        var entry = Map.of("event", "display_underrun", "uptime_ms", 1800,
                "display_mode", "speaking", "animation_active", true, "underrun_count", 3);
        assertTrue(diagnostics.upload(auth, device, timingBody(until, entry)).get("accepted"));
        assertTrue(diagnostics.upload(auth, device, timingBody(until, entry)).get("accepted"));
        var logs = (List<?>) diagnostics.read(device).get("logs");
        assertEquals(1, logs.size());
        var stored = json.valueToTree(logs.getFirst()).path("entries").get(0);
        assertEquals("speaking", stored.path("display_mode").asText());
        assertEquals(3, stored.path("underrun_count").asInt());
        assertTrue(stored.path("animation_active").asBoolean());
        assertEquals(400, assertThrows(SpeakerFailure.class, () -> diagnostics.upload(auth, device,
                timingBody(until, Map.of("event", "display_underrun", "display_mode", "listening")))).status);
        assertEquals(400, assertThrows(SpeakerFailure.class, () -> diagnostics.upload(auth, device,
                timingBody(until, Map.of("event", "display_underrun", "display_mode", "sleep",
                        "animation_active", false, "underrun_count", 0)))).status);
        assertEquals(409, assertThrows(SpeakerFailure.class,
                () -> diagnostics.upload(auth, device, timingBody(until + 1, entry))).status);
    }

    @Test void ttsPlaybackReportsRetainPendingWritesAndRejectInvalidStages() throws Exception {
        devices.heartbeat(devices.authenticate(auth, device), "p4", "2.6.11", true);
        long until = enable();
        var samples = List.of(
                Map.of("phase", "write_started", "uptime_ms", 4350, "count", 1, "code", 0),
                Map.of("phase", "write_pending", "uptime_ms", 7600, "count", 1, "code", 3250));
        var report = Map.of("event", "tts_playback", "uptime_ms", 7600, "playback_stages", samples);
        assertTrue(diagnostics.upload(auth, device, timingBody(until, report)).get("accepted"));
        var reports = (List<?>) diagnostics.read(device).get("logs");
        var stored = json.valueToTree(reports.getFirst()).path("entries").get(0).path("playback_stages");
        assertEquals("write_pending", stored.get(1).path("phase").asText());
        assertEquals(3250, stored.get(1).path("code").asInt());
        assertEquals(400, assertThrows(SpeakerFailure.class, () -> diagnostics.upload(auth, device,
                timingBody(until, Map.of("event", "tts_playback", "playback_stages",
                        List.of(Map.of("phase", "unexpected", "count", 1)))))).status);
        assertEquals(400, assertThrows(SpeakerFailure.class, () -> diagnostics.upload(auth, device,
                timingBody(until, Map.of("event", "tts_playback")))).status);
    }

    @Test void ttsTransportReportsRetainRoundCountersAndValidateMissingStop() throws Exception {
        devices.heartbeat(devices.authenticate(auth, device), "p4", "2.6.12", true);
        long until = enable();
        var sample = Map.of("received_frames", 165, "first_packet_ms", 1000, "last_packet_ms", 21000,
                "max_receive_gap_ms", 900, "stop_received", false, "channel_closed", true);
        var entry = Map.of("event", "tts_transport", "trace_id", "a".repeat(32), "transport", sample);
        assertTrue(diagnostics.upload(auth, device, timingBody(until, entry)).get("accepted"));
        var logs = (List<?>) diagnostics.read(device).get("logs");
        var stored = json.valueToTree(logs.getFirst()).path("entries").get(0);
        assertEquals("a".repeat(32), stored.path("trace_id").asText());
        assertEquals(165, stored.path("transport").path("received_frames").asInt());
        assertFalse(stored.path("transport").path("stop_received").asBoolean());
        assertTrue(stored.path("transport").path("channel_closed").asBoolean());
        assertEquals(400, assertThrows(SpeakerFailure.class, () -> diagnostics.upload(auth, device,
                timingBody(until, Map.of("event", "tts_transport", "transport", sample)))).status);
        assertEquals(400, assertThrows(SpeakerFailure.class, () -> diagnostics.upload(auth, device,
                timingBody(until, Map.of("event", "tts_transport", "trace_id", "a".repeat(32), "transport",
                        Map.of("received_frames", 10, "stop_received", false))))).status);
    }

    @Test void genericMetricsAndLegacyEventsCanShareAReport() throws Exception {
        devices.heartbeat(devices.authenticate(auth, device), "p4", "2.6.15", true);
        long until = enable();
        String trace = "a".repeat(32);
        var count = Map.of("event", "metric", "uptime_ms", 1800, "trace_id", trace,
                "metric", Map.of("name", "audio.queue_peak", "label", "音频队列峰值", "value", 12, "unit", "帧"));
        var flag = Map.of("event", "metric", "uptime_ms", 1900,
                "metric", Map.of("name", "audio.stop_received", "value", false));
        var error = Map.of("event", "metric", "uptime_ms", 2000,
                "metric", Map.of("name", "audio.write_error_code", "value", -5));
        var latency = Map.of("event", "metric", "uptime_ms", 2100,
                "metric", Map.of("name", "audio.write_time", "value", 24.5, "unit", "ms"));
        var boot = Map.of("event", "boot", "uptime_ms", 0, "code", 4);

        assertTrue(diagnostics.upload(auth, device, reportBody(until, List.of(boot, count, flag, error, latency)))
                .get("accepted"));
        var logs = (List<?>) diagnostics.read(device).get("logs");
        var entries = json.valueToTree(logs.getFirst()).path("entries");
        assertEquals("boot", entries.get(0).path("event").asText());
        assertFalse(entries.get(0).has("metric"));
        assertEquals(trace, entries.get(1).path("trace_id").asText());
        assertEquals(json.valueToTree(count.get("metric")), entries.get(1).path("metric"));
        assertEquals(json.valueToTree(false), entries.get(2).path("metric").path("value"));
        assertEquals(-5, entries.get(3).path("metric").path("value").asInt());
        assertEquals(24.5, entries.get(4).path("metric").path("value").asDouble());
        assertEquals(409, assertThrows(SpeakerFailure.class,
                () -> diagnostics.upload(auth, device, timingBody(until + 1, count))).status);
    }

    @Test void genericMetricsRejectInvalidMetadataAndNonScalarValues() throws Exception {
        devices.heartbeat(devices.authenticate(auth, device), "p4", "2.6.15", true);
        long until = enable();
        for (var metric : List.of(
                Map.of("name", "audio peak", "value", 12),
                Map.of("name", "audio.peak", "label", "<b>label</b>", "value", 12),
                Map.of("name", "audio.peak", "value", "12"),
                Map.of("name", "audio.peak", "value", Map.of("count", 12)),
                Map.of("name", "audio.peak", "value", 9_007_199_254_740_992L),
                Map.of("name", "audio.peak", "value", true, "unit", "ms"))) {
            assertEquals(400, assertThrows(SpeakerFailure.class, () -> diagnostics.upload(auth, device,
                    timingBody(until, Map.of("event", "metric", "metric", metric)))).status);
        }
        assertEquals(400, assertThrows(SpeakerFailure.class, () -> diagnostics.upload(auth, device,
                timingBody(until, Map.of("event", "metric")))).status);
        assertEquals(400, assertThrows(SpeakerFailure.class, () -> diagnostics.upload(auth, device,
                timingBody(until, Map.of("event", "boot", "metric", Map.of("name", "audio.peak", "value", 12))))).status);
        assertEquals(List.of(), diagnostics.read(device).get("logs"));
    }

    private MockHttpServletRequest timingBody(long until, Map<String, ?> entry) throws Exception {
        return reportBody(until, List.of(entry));
    }

    private MockHttpServletRequest reportBody(long until, List<? extends Map<String, ?>> entries) throws Exception {
        var request = new MockHttpServletRequest();
        request.setContent(json.writeValueAsBytes(Map.of("report_id", "2".repeat(32),
                "diagnostics_until_ms", until, "firmware_version", "2.6.7", "app_sha256", "a".repeat(64),
                "entries", entries)));
        return request;
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
