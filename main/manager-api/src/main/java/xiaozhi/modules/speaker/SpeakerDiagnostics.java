package xiaozhi.modules.speaker;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.validation.Valid;
import jakarta.validation.Validator;
import jakarta.validation.constraints.*;
import org.apache.shiro.authz.annotation.RequiresPermissions;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.web.bind.annotation.*;
import xiaozhi.modules.security.user.SecurityUser;

import java.io.IOException;
import java.util.List;
import java.util.Map;

import static xiaozhi.modules.speaker.SpeakerSupport.yes;

@RestController
@ConditionalOnProperty(name = "speaker.enabled", havingValue = "true")
public class SpeakerDiagnostics {
    private static final long WINDOW_MS = 30 * 60_000L;
    private static final int MAX_BODY_BYTES = 8_192;
    private final JdbcTemplate db;
    private final SpeakerDevices devices;
    private final ObjectMapper json;
    private final Validator validator;

    public SpeakerDiagnostics(JdbcTemplate db, SpeakerDevices devices, ObjectMapper json, Validator validator) {
        this.db = db;
        this.devices = devices;
        this.json = json;
        this.validator = validator;
    }

    public record Toggle(@NotNull Boolean enabled) {}

    public record PlaybackStage(
            @NotNull @Pattern(regexp = "start_received|start_handled|packet_received|packet_dropped|decoded|decode_error"
                    + "|output_begin|write_started|write_ok|write_error|write_pending|stop_received|stop_handled"
                    + "|drained|channel_closed|channel_cleaned") String phase,
            @Min(0) long uptime_ms,
            @Min(1) @Max(4_294_967_295L) long count,
            int code) {}

    public record TransportSample(
            @NotNull @Min(0) @Max(4_294_967_295L) Long received_frames,
            @NotNull @Min(0) Long first_packet_ms,
            @NotNull @Min(0) Long last_packet_ms,
            @NotNull @Min(0) Long max_receive_gap_ms,
            @NotNull Boolean stop_received,
            @NotNull Boolean channel_closed) {
        @AssertTrue
        @com.fasterxml.jackson.annotation.JsonIgnore
        public boolean isPacketTimingConsistent() {
            if (received_frames == null || first_packet_ms == null || last_packet_ms == null
                    || max_receive_gap_ms == null) return true;
            if (received_frames == 0) return first_packet_ms == 0 && last_packet_ms == 0 && max_receive_gap_ms == 0;
            return first_packet_ms <= last_packet_ms && max_receive_gap_ms <= last_packet_ms - first_packet_ms;
        }
    }

    public record Entry(
            @NotNull @Pattern(regexp = "boot|state|voice_network_error|ota_check_failed|ota_upgrade_failed"
                    + "|heartbeat_failed|voice_timing|display_underrun|tts_playback|tts_transport") String event,
            @Min(0) long uptime_ms,
            @Min(0) @Max(10) int state,
            int code,
            @Min(0) @Max(64_000_000) int free_heap_bytes,
            @Min(0) @Max(64_000_000) int min_free_heap_bytes,
            @Pattern(regexp = "[a-f0-9]{32}") String trace_id,
            @Pattern(regexp = "wake|channel_open|listening|first_mic_frame|stt_received|first_audio_received|first_output|playback_drained") String phase,
            @Min(0) @Max(600_000) Integer elapsed_ms,
            @Pattern(regexp = "idle|connecting|listening|waiting|speaking") String display_mode,
            Boolean animation_active,
            @Min(0) @Max(4_294_967_295L) Long underrun_count,
            @Size(min = 1, max = 16) List<@NotNull @Valid PlaybackStage> playback_stages,
            @Valid TransportSample transport) {
        @AssertTrue
        @com.fasterxml.jackson.annotation.JsonIgnore
        public boolean isTimingComplete() {
            if ("voice_timing".equals(event)) return trace_id != null && phase != null && elapsed_ms != null;
            if ("tts_transport".equals(event)) return trace_id != null && phase == null && elapsed_ms == null;
            return trace_id == null && phase == null && elapsed_ms == null;
        }

        @AssertTrue
        @com.fasterxml.jackson.annotation.JsonIgnore
        public boolean isDisplaySampleComplete() {
            if ("display_underrun".equals(event)) {
                return display_mode != null && animation_active != null && underrun_count != null;
            }
            return display_mode == null && animation_active == null && underrun_count == null;
        }

        @AssertTrue
        @com.fasterxml.jackson.annotation.JsonIgnore
        public boolean isPlaybackSampleComplete() {
            return "tts_playback".equals(event) == (playback_stages != null);
        }

        @AssertTrue
        @com.fasterxml.jackson.annotation.JsonIgnore
        public boolean isTransportSampleComplete() {
            return "tts_transport".equals(event) == (transport != null);
        }
    }

    public record Report(
            @NotNull @Pattern(regexp = "[a-f0-9]{32}") String report_id,
            @Positive long diagnostics_until_ms,
            @NotNull @Pattern(regexp = "[A-Za-z0-9][A-Za-z0-9_.+-]{0,63}") String firmware_version,
            @NotNull @Pattern(regexp = "[a-f0-9]{64}") String app_sha256,
            @NotNull @Size(min = 1, max = 16) List<@NotNull @Valid Entry> entries) {}

    @GetMapping("/admin/speaker/devices/{id}/diagnostics")
    @RequiresPermissions("sys:role:superAdmin")
    public Map<String, Object> read(@PathVariable String id) {
        return detail(id);
    }

    @PostMapping("/admin/speaker/devices/{id}/diagnostics")
    @RequiresPermissions("sys:role:superAdmin")
    public Map<String, Object> toggle(@PathVariable String id, @Valid @RequestBody Toggle toggle) {
        devices.transactions.executeWithoutResult(tx -> {
            var device = findDevice(id, true);
            if (toggle.enabled() && !yes(device.get("diagnostics_supported"))) {
                throw new SpeakerFailure(409, "DEVICE_DIAGNOSTICS_UNSUPPORTED");
            }
            long now = System.currentTimeMillis();
            long previous = ((Number) device.get("diagnostics_until_ms")).longValue();
            if (toggle.enabled() == (previous > now)) return;
            db.update("UPDATE speaker_device_state SET diagnostics_until_ms=? WHERE device_id=?",
                    toggle.enabled() ? now + WINDOW_MS : 0L, id);
            devices.event(id, toggle.enabled() ? "diagnostics-on" : "diagnostics-off", "admin:" + SecurityUser.getUserId());
        });
        return detail(id);
    }

    @PostMapping("/speaker/v1/diagnostics")
    public Map<String, Boolean> upload(
            @RequestHeader(name = "Authorization", required = false) String authorization,
            @RequestHeader(name = "Device-Id", required = false) String deviceId,
            HttpServletRequest request) {
        var identity = devices.authenticate(authorization, deviceId);
        var report = readReport(request);
        var entries = entriesJson(report.entries());
        devices.transactions.executeWithoutResult(tx -> {
            var device = devices.load(identity, true);
            long until = ((Number) device.get("diagnostics_until_ms")).longValue();
            long now = System.currentTimeMillis();
            if (until <= now || until != report.diagnostics_until_ms() || !yes(device.get("diagnostics_supported"))) {
                throw new SpeakerFailure(409, "DEVICE_DIAGNOSTICS_INACTIVE");
            }
            db.update("INSERT INTO speaker_device_diagnostic_log(device_id,report_id,firmware_version,app_sha256,entries,received_at_ms) "
                            + "VALUES(?,?,?,?,?,?) ON DUPLICATE KEY UPDATE report_id=VALUES(report_id)",
                    identity.id(), report.report_id(), report.firmware_version(), report.app_sha256(), entries, now);
            // The locked device row serializes insert and retention for this device.
            var expired = db.queryForList("SELECT report_id FROM speaker_device_diagnostic_log WHERE device_id=? "
                    + "ORDER BY received_at_ms DESC,report_id DESC LIMIT 50 OFFSET 50", String.class, identity.id());
            for (var id : expired) {
                db.update("DELETE FROM speaker_device_diagnostic_log WHERE device_id=? AND report_id=?", identity.id(), id);
            }
        });
        return Map.of("accepted", true);
    }

    private Report readReport(HttpServletRequest request) {
        try {
            byte[] body = request.getInputStream().readNBytes(MAX_BODY_BYTES + 1);
            if (body.length > MAX_BODY_BYTES) throw new SpeakerFailure(413, "DIAGNOSTICS_REPORT_TOO_LARGE");
            var report = json.readValue(body, Report.class);
            if (report != null && validator.validate(report).isEmpty()) return report;
        } catch (IOException ignored) {
            // Avoid logging device-supplied payloads.
        }
        throw new SpeakerFailure(400, "INVALID_DIAGNOSTICS_REPORT");
    }

    private String entriesJson(List<Entry> entries) {
        try {
            return json.writeValueAsString(entries);
        } catch (JsonProcessingException ex) {
            throw new IllegalStateException("Cannot serialize validated diagnostic entries", ex);
        }
    }

    private Map<String, Object> detail(String id) {
        var device = findDevice(id, false);
        long until = ((Number) device.get("diagnostics_until_ms")).longValue();
        var logs = db.query("SELECT report_id,firmware_version,app_sha256,entries,received_at_ms "
                + "FROM speaker_device_diagnostic_log WHERE device_id=? "
                + "ORDER BY received_at_ms DESC,report_id DESC LIMIT 50", (rs, rowNum) -> Map.of(
                "report_id", rs.getString("report_id"),
                "firmware_version", rs.getString("firmware_version"),
                "app_sha256", rs.getString("app_sha256"),
                "entries", parseEntries(rs.getString("entries")),
                "received_at_ms", rs.getLong("received_at_ms")), id);
        long now = System.currentTimeMillis();
        return Map.of("supported", yes(device.get("diagnostics_supported")), "until_ms", until,
                "enabled", until > now, "server_time_ms", now, "logs", logs);
    }

    private Object parseEntries(String entries) {
        try {
            return json.readTree(entries);
        } catch (JsonProcessingException ex) {
            throw new IllegalStateException("Invalid stored diagnostic entries", ex);
        }
    }

    private Map<String, Object> findDevice(String id, boolean lock) {
        var devices = db.queryForList("SELECT diagnostics_supported,diagnostics_until_ms FROM speaker_device_state "
                + "WHERE device_id=?" + (lock ? " FOR UPDATE" : ""), id);
        if (devices.isEmpty()) throw new SpeakerFailure(404, "DEVICE_NOT_FOUND");
        return devices.getFirst();
    }
}
