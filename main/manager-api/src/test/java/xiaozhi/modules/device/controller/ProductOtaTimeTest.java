package xiaozhi.modules.device.controller;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

import java.time.Instant;
import java.time.ZoneId;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.test.util.ReflectionTestUtils;
import xiaozhi.modules.device.dto.DeviceReportReqDTO;
import xiaozhi.modules.device.service.DeviceService;
import xiaozhi.modules.sys.service.SysParamsService;

class ProductOtaTimeTest {
    @ParameterizedTest
    @ValueSource(strings = {"Asia/Hong_Kong", "UTC", "Asia/Kolkata", "America/New_York"})
    void discoveryProvidesNumericFirmwareClockWithoutVoiceCredentials(String zoneId) throws Exception {
        var devices = mock(DeviceService.class);
        var params = mock(SysParamsService.class);
        var controller = new OTAController(devices, params);
        ReflectionTestUtils.setField(controller, "productControl", true);
        ReflectionTestUtils.setField(controller, "productOrigin", "https://speaker.example:18443");
        var zone = ZoneId.of(zoneId);
        ReflectionTestUtils.setField(controller, "productTimeZone", zone);
        long before = System.currentTimeMillis();
        var response = controller.checkOTAVersion(new DeviceReportReqDTO(), "02:00:00:00:00:01", null);
        long after = System.currentTimeMillis();
        var body = new ObjectMapper().readTree(response.getBody());
        var time = body.path("server_time");
        assertTrue(time.path("timestamp").isIntegralNumber(), "Firmware requires a numeric millisecond timestamp");
        long timestamp = time.path("timestamp").asLong();
        assertTrue(timestamp >= before && timestamp <= after);
        assertTrue(time.path("timezone_offset").isIntegralNumber());
        int offset = time.path("timezone_offset").asInt();
        assertEquals(zone.getRules().getOffset(Instant.ofEpochMilli(timestamp)).getTotalSeconds() / 60, offset);
        // The firmware adds the offset to the epoch before rendering its local clock.
        assertEquals(Instant.ofEpochMilli(timestamp).atZone(zone).toLocalDateTime(),
                Instant.ofEpochMilli(timestamp + offset * 60000L).atOffset(java.time.ZoneOffset.UTC).toLocalDateTime());
        assertEquals(2, body.size());
        assertEquals(1, body.path("device_control").path("version").asInt());
        assertEquals("https://speaker.example:18443/xiaozhi/device-control/v1/",
                body.path("device_control").path("base_url").asText());
        assertEquals("no-store", response.getHeaders().getFirst("Cache-Control"));
        verifyNoInteractions(devices, params);
    }
}
