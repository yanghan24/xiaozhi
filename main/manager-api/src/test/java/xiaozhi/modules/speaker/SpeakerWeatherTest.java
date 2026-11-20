package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import org.mockito.ArgumentMatchers;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.SimpleTransactionStatus;

class SpeakerWeatherTest {
    @Test
    void standbyWeatherAuthorizationDoesNotNeedVoiceConfiguration() {
        var db = mock(JdbcTemplate.class);
        var manager = mock(PlatformTransactionManager.class);
        when(manager.getTransaction(any())).thenReturn(new SimpleTransactionStatus());
        var devices = spy(new SpeakerDevices(db, null, manager));
        var identity = new SpeakerDevices.Identity("speaker", "fixture");
        var device = Map.<String, Object>of("customer_id", "customer");
        doReturn(device).when(devices).load(identity, true);
        when(db.queryForMap("SELECT display_name FROM speaker_customer WHERE id=?", "customer"))
                .thenReturn(Map.of("display_name", "fixture"));
        var voice = mock(SpeakerVoice.class);
        when(voice.ticket(device)).thenThrow(new SpeakerFailure(503, "DEFAULT_AGENT_REQUIRED"));
        var operations = spy(new SpeakerOperations(db, devices, null, voice));
        doReturn("authorized").when(operations).useStatus(device);

        var state = operations.state(identity);
        assertEquals(true, state.get("standby_weather_allowed"));
        assertEquals("default_agent_required", state.get("use_status"));
        assertNull(state.get("voice"));
    }

    @Test
    void checksDeviceEntitlementBeforeTrustingTheIngressAddress() {
        SpeakerDevices devices = mock(SpeakerDevices.class);
        SpeakerOperations operations = mock(SpeakerOperations.class);
        SpeakerWeather weather = mock(SpeakerWeather.class);
        var controller = new SpeakerDeviceController(devices, null, operations, weather);
        var request = new MockHttpServletRequest();
        request.setRemoteAddr("127.0.0.1");
        request.addHeader("X-Xiaozhi-Ingress", "public");
        request.addHeader("X-Xiaozhi-Device-IP", "1.2.3.4");
        assertThrows(SpeakerFailure.class, () -> controller.weather(null, "speaker", request));
        verifyNoInteractions(weather);

        String auth = "Bearer " + "a".repeat(43);
        var identity = SpeakerDevices.parseIdentity(auth, "speaker");
        var device = Map.<String, Object>of("device_id", "speaker");
        when(devices.load(identity, false)).thenReturn(device);
        when(operations.useStatus(device)).thenReturn("unbound", "authorized");
        assertThrows(SpeakerFailure.class, () -> controller.weather(auth, "speaker", request));
        verifyNoInteractions(weather);
        controller.weather(auth, "speaker", request);
        verify(weather).current("1.2.3.4");
    }

    @Test
    void weatherUsesDeviceIpThenAdcodeAndCachesWithoutReturningCredential() throws Exception {
        var services = mock(SpeakerThirdPartyServices.class);
        when(services.amapConfiguration()).thenReturn(
                Optional.of(new SpeakerThirdPartyServices.AmapConfiguration(1, "fake-key")));
        var http = mock(HttpClient.class);
        when(http.send(any(HttpRequest.class), ArgumentMatchers.<HttpResponse.BodyHandler<byte[]>>any()))
                .thenAnswer(invocation -> {
                    HttpRequest request = invocation.getArgument(0);
                    assertEquals("restapi.amap.com", request.uri().getHost());
                    assertTrue(request.uri().getQuery().contains("key=fake-key"));
                    return switch (request.uri().getPath()) {
                        case "/v3/ip" -> {
                            assertTrue(request.uri().getQuery().contains("ip=1.2.3.4"));
                            yield response("""
                                    {"status":"1","adcode":"430100","city":"长沙市"}
                                    """);
                        }
                        case "/v3/weather/weatherInfo" -> {
                            assertTrue(request.uri().getQuery().contains("city=430100"));
                            assertTrue(request.uri().getQuery().contains("extensions=base"));
                            yield response("""
                                    {"status":"1","lives":[{"city":"长沙市","weather":"阴","temperature":"20",
                                      "humidity":"72","winddirection":"北","windpower":"2"}]}
                                    """);
                        }
                        default -> throw new AssertionError("Unknown request path");
                    };
                });
        var weather = new SpeakerWeather(services, new ObjectMapper(), http);
        for (String address : new String[]{"127.0.0.1", "10.0.0.1", "100.64.0.1", "::1", "1.2.3.4, 5.6.7.8"}) {
            assertEquals("unavailable", weather.current(address).status());
        }
        verifyNoInteractions(http);
        var report = weather.current("1.2.3.4");
        assertEquals("ready", report.status());
        assertEquals("长沙市", report.city());
        assertEquals("20", report.temperature());
        assertEquals("72", report.humidity());
        assertFalse(report.toString().contains("fake-key"));
        assertEquals(report, weather.current("1.2.3.4"));
        verify(http, times(2)).send(any(HttpRequest.class), ArgumentMatchers.<HttpResponse.BodyHandler<byte[]>>any());
    }

    @SuppressWarnings("unchecked")
    private static HttpResponse<byte[]> response(String json) {
        var response = (HttpResponse<byte[]>) mock(HttpResponse.class);
        when(response.statusCode()).thenReturn(200);
        when(response.body()).thenReturn(json.getBytes(StandardCharsets.UTF_8));
        return response;
    }
}
