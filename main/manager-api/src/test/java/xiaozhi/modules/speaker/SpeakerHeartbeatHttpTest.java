package xiaozhi.modules.speaker;

import java.time.Instant;
import java.time.ZoneId;
import java.util.Map;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;

import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

class SpeakerHeartbeatHttpTest {
    @ParameterizedTest
    @CsvSource({
            "Asia/Shanghai, 2026-11-23T01:00:00Z, 480",
            "America/New_York, 2026-01-23T01:00:00Z, -300",
            "America/New_York, 2026-07-23T01:00:00Z, -240"
    })
    void heartbeatReturnsTheOffsetForItsUtcTimestamp(String zone, String instant, int offset) throws Exception {
        String authorization = "Bearer fixture";
        String deviceId = "device-a";
        long timestamp = Instant.parse(instant).toEpochMilli();
        var devices = mock(SpeakerDevices.class);
        var identity = new SpeakerDevices.Identity(deviceId, "hash");
        when(devices.authenticate(authorization, deviceId)).thenReturn(identity);
        when(devices.heartbeat(identity, "p4", "2.6.21", true)).thenReturn(Map.of(
                "device_id", deviceId, "server_time_ms", timestamp,
                "heartbeat_interval_ms", 20_000, "diagnostics_until_ms", 0L));
        var controller = new SpeakerDeviceController(devices, null, null, null);
        ReflectionTestUtils.setField(controller, "productTimeZone", ZoneId.of(zone));
        var http = MockMvcBuilders.standaloneSetup(controller)
                .setControllerAdvice(new SpeakerProtocolResponse()).build();

        http.perform(post("/speaker/v1/heartbeat").header("Authorization", authorization)
                        .header("Device-Id", deviceId).contentType("application/json")
                        .content("{\"board_type\":\"p4\",\"firmware_version\":\"2.6.21\",\"diagnostics_supported\":true}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.device_id").value(deviceId))
                .andExpect(jsonPath("$.server_time_ms").isNumber())
                .andExpect(jsonPath("$.server_time_ms").value(timestamp))
                .andExpect(jsonPath("$.timezone_offset").isNumber())
                .andExpect(jsonPath("$.timezone_offset").value(offset))
                .andExpect(jsonPath("$.heartbeat_interval_ms").value(20_000))
                .andExpect(jsonPath("$.diagnostics_until_ms").value(0));
    }
}
