package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.math.BigDecimal;
import java.net.http.HttpClient;
import java.net.http.HttpHeaders;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.security.KeyPairGenerator;
import java.security.PublicKey;
import java.security.Signature;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
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
        var device = Map.<String, Object>of("customer_id", "customer", "reauthorization_until_ms", 0L);
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
        assertNull(report.feelsLike());
        assertNull(report.aqi());
        assertFalse(report.toString().contains("fake-key"));
        assertEquals(report, weather.current("1.2.3.4"));
        verify(http, times(2)).send(any(HttpRequest.class), ArgumentMatchers.<HttpResponse.BodyHandler<byte[]>>any());
    }

    @Test
    void standbyAndVoiceShareCurrentWeatherAndSignRequestsWithJwt() throws Exception {
        var json = new ObjectMapper();
        var services = mock(SpeakerThirdPartyServices.class);
        var key = KeyPairGenerator.getInstance("Ed25519").generateKeyPair();
        var configuration = new SpeakerThirdPartyServices.QWeatherConfiguration(2, "https://fixture.qweatherapi.com",
                "developer", "project", "credential", "长沙", Base64.getEncoder().encodeToString(key.getPrivate().getEncoded()));
        when(services.qweatherConfiguration()).thenReturn(Optional.of(configuration));
        var http = mock(HttpClient.class);
        when(http.send(any(HttpRequest.class), ArgumentMatchers.<HttpResponse.BodyHandler<byte[]>>any()))
                .thenAnswer(invocation -> {
                    HttpRequest request = invocation.getArgument(0);
                    verifyQWeatherJwt(json, key.getPublic(), request);
                    return qweatherResponse(request);
                });
        when(http.sendAsync(any(HttpRequest.class), ArgumentMatchers.<HttpResponse.BodyHandler<byte[]>>any()))
                .thenAnswer(invocation -> {
                    HttpRequest request = invocation.getArgument(0);
                    verifyQWeatherJwt(json, key.getPublic(), request);
                    assertEquals("/airquality/v1/current/28.21/112.94", request.uri().getPath());
                    return CompletableFuture.completedFuture(response("""
                            {"indexes":[{"code":"qaqi","aqi":59},{"code":"cn-mee","aqi":72.4},
                                        {"code":"cn-mee","aqi":501},{"code":"cn-mee","aqi":72}]}
                            """));
                });
        var weather = new SpeakerWeather(services, json, http);
        var voice = weather.query("10.0.0.1", null);
        var voiceCurrent = (SpeakerWeather.Report) voice.get("current");
        assertFalse(json.valueToTree(voiceCurrent).has("aqi"));
        assertFalse(json.valueToTree(voiceCurrent).has("feelsLike"));
        assertEquals(List.of(new QWeatherClient.Day("2026-11-26", "阴", "12", "16")), voice.get("forecast"));
        verify(http, never()).sendAsync(any(HttpRequest.class), ArgumentMatchers.<HttpResponse.BodyHandler<byte[]>>any());

        var report = weather.current("10.0.0.1");
        assertEquals("长沙", report.city());
        assertEquals("13", report.temperature());
        assertEquals("69", report.humidity());
        assertEquals(new BigDecimal("11.5"), report.feelsLike());
        assertEquals(72, report.aqi());
        assertTrue(json.valueToTree(report).path("feelsLike").isNumber());
        assertTrue(json.valueToTree(report).path("aqi").isIntegralNumber());
        assertEquals(voiceCurrent, report.withStandbyDetails(null, null));
        assertEquals(report, weather.current("10.0.0.2"));
        verify(http, times(4)).send(any(HttpRequest.class), ArgumentMatchers.<HttpResponse.BodyHandler<byte[]>>any());
        verify(http).sendAsync(any(HttpRequest.class), ArgumentMatchers.<HttpResponse.BodyHandler<byte[]>>any());
    }

    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void airQualityFailureOrTimeoutKeepsWeatherAvailable(boolean timedOut) throws Exception {
        var services = mock(SpeakerThirdPartyServices.class);
        var key = KeyPairGenerator.getInstance("Ed25519").generateKeyPair();
        when(services.qweatherConfiguration()).thenReturn(Optional.of(
                new SpeakerThirdPartyServices.QWeatherConfiguration(2, "https://fixture.qweatherapi.com",
                        "developer", "project", "credential", "长沙",
                        Base64.getEncoder().encodeToString(key.getPrivate().getEncoded()))));
        var http = mock(HttpClient.class);
        when(http.send(any(HttpRequest.class), ArgumentMatchers.<HttpResponse.BodyHandler<byte[]>>any()))
                .thenAnswer(invocation -> qweatherResponse(invocation.getArgument(0)));
        var pending = new CompletableFuture<HttpResponse<byte[]>>();
        if (!timedOut) {
            var unavailable = response("{}");
            when(unavailable.statusCode()).thenReturn(503);
            pending.complete(unavailable);
        }
        when(http.sendAsync(any(HttpRequest.class), ArgumentMatchers.<HttpResponse.BodyHandler<byte[]>>any()))
                .thenReturn(pending);

        var weather = new SpeakerWeather(services, new ObjectMapper(), http);
        var report = weather.current("10.0.0.1");
        assertEquals("ready", report.status());
        assertEquals("阴", report.condition());
        assertEquals("13", report.temperature());
        assertEquals(new BigDecimal("11.5"), report.feelsLike());
        assertNull(report.aqi());
        assertEquals(timedOut, pending.isCancelled());
        assertEquals(report, weather.current("10.0.0.2"));
        verify(http, times(2)).send(any(HttpRequest.class), ArgumentMatchers.<HttpResponse.BodyHandler<byte[]>>any());
        verify(http).sendAsync(any(HttpRequest.class), ArgumentMatchers.<HttpResponse.BodyHandler<byte[]>>any());
    }

    private static void verifyQWeatherJwt(ObjectMapper json, PublicKey key, HttpRequest request) throws Exception {
        assertEquals("fixture.qweatherapi.com", request.uri().getHost());
        assertFalse(request.uri().toString().contains("key="));
        String authorization = request.headers().firstValue("Authorization").orElseThrow();
        assertTrue(authorization.startsWith("Bearer "));
        String[] jwt = authorization.substring(7).split("\\.");
        var decoder = Base64.getUrlDecoder();
        var header = json.readTree(decoder.decode(jwt[0]));
        var claims = json.readTree(decoder.decode(jwt[1]));
        assertEquals("EdDSA", header.path("alg").asText());
        assertEquals("credential", header.path("kid").asText());
        assertEquals("developer", claims.path("iss").asText());
        assertEquals("project", claims.path("sub").asText());
        assertEquals(900, claims.path("exp").asLong() - claims.path("iat").asLong());
        var signature = Signature.getInstance("Ed25519");
        signature.initVerify(key);
        signature.update((jwt[0] + "." + jwt[1]).getBytes(StandardCharsets.US_ASCII));
        assertTrue(signature.verify(decoder.decode(jwt[2])));
    }

    private static HttpResponse<byte[]> qweatherResponse(HttpRequest request) {
        return switch (request.uri().getPath()) {
            case "/geo/v2/city/lookup" -> response("""
                    {"code":"200","location":[{"name":"长沙","lat":"28.213","lon":"112.941"}]}
                    """);
            case "/weather/v1/current/28.21/112.94" -> response("""
                    {"validTime":"2026-11-26T12:00:00+08:00","condition":{"text":"阴"},
                     "temperature":{"value":13.4,"unit":"°C"},"feelsLike":{"value":11.4,"unit":"°C"},"humidity":0.69}
                    """);
            case "/weather/v1/daily/28.21/112.94" -> response("""
                    {"days":[{"daytime":{"forecastStartTime":"2026-11-26T07:00:00+08:00","condition":{"text":"阴"}},
                      "temperatureMin":{"value":12},"temperatureMax":{"value":16}}]}
                    """);
            default -> throw new AssertionError("Unknown request path");
        };
    }

    @SuppressWarnings("unchecked")
    private static HttpResponse<byte[]> response(String json) {
        var response = (HttpResponse<byte[]>) mock(HttpResponse.class);
        when(response.statusCode()).thenReturn(200);
        when(response.headers()).thenReturn(HttpHeaders.of(Map.of(), (name, value) -> true));
        when(response.body()).thenReturn(json.getBytes(StandardCharsets.UTF_8));
        return response;
    }
}
