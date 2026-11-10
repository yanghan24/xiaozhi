package xiaozhi.modules.customer;

import static org.junit.jupiter.api.Assertions.*;
import java.util.Map;
import org.junit.jupiter.api.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.context.annotation.Import;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

@SpringBootTest(webEnvironment=SpringBootTest.WebEnvironment.RANDOM_PORT,properties={
    "server.servlet.context-path=/xiaozhi","server.forward-headers-strategy=native","renren.xss.enabled=true",
    "device-control.enabled=true","device-control.public-origin=https://bridge.hncig.cn:18443",
    "device-control.voice-enabled=true","device-control.voice-service-secret=isolated-voice-test-service-secret-123456"})
@Import(CustomerTestTime.class)
class CustomerVoicePermissionHttpTest {
    static final ControlledMarket market=new ControlledMarket();
    @DynamicPropertySource static void config(DynamicPropertyRegistry registry) {
        registry.add("customer.oidc.config-file",()->market.config.toString());
        registry.add("customer.oidc.refresh-delay-ms",()->"200");
    }
    @Autowired TestRestTemplate http;
    @Autowired JdbcTemplate db;
    @org.springframework.boot.test.mock.mockito.MockBean(name="deviceControlClock") java.time.Clock deviceClock;
    @Autowired @org.springframework.beans.factory.annotation.Qualifier("customerTime") java.time.Clock customerClock;
    BindingTestClient client;
    @BeforeEach void setup(){client=new BindingTestClient(http,db,market);org.mockito.Mockito.when(deviceClock.millis()).thenAnswer(call->customerClock.millis());}
    @AfterEach void reset(){CustomerTestTime.offset.set(0);CustomerTestTime.frozen.set(0);}
    @Test void explicitOwnerResumeEnablesOnlyTargetAndIssuesBoundedVoiceCredentials() {
        String subject=java.util.UUID.randomUUID().toString();
        var a=client.device();var b=client.device();
        var fa=client.scan(a);var fb=client.scan(b);client.login(fa,null,subject);
        String sa=BindingTestClient.data(client.confirm(fa,"bind")).get("device_session_id").toString();
        client.login(fb,null,subject);
        String sb=BindingTestClient.data(client.confirm(fb,"bind")).get("device_session_id").toString();
        assertEquals("allowed",client.state(a).get("use_permission"));
        client.post(fa,"device-sessions/"+sa+"/sign-out","stop");client.post(fb,"device-sessions/"+sb+"/sign-out","stop");
        var resume=client.scan(client.request(a,"auto"));client.login(resume,null,subject);
        assertTrue(client.page(resume).contains("恢复本台使用"));
        assertEquals("user_signed_out",client.state(a).get("reason"));
        assertEquals(200,client.confirm(resume,"resume").getStatusCode().value());
        assertTrue(client.page(resume).contains("恢复操作已完成"));
        var state=client.state(a);
        assertEquals("allowed",state.get("use_permission"));assertEquals("ready",state.get("reason"));
        assertEquals("user_signed_out",client.state(b).get("reason"));
        var lease=(Map<?,?>)state.get("lease");var voice=(Map<?,?>)state.get("voice");
        assertEquals(60000,((Number)lease.get("expires_in_ms")).intValue());
        assertEquals(((Number)state.get("server_time_ms")).longValue()+60000,((Number)lease.get("expires_at_ms")).longValue());
        assertEquals("wss://bridge.hncig.cn:18443/xiaozhi/v1/",voice.get("url"));
        assertTrue(((Number)voice.get("expires_at_ms")).longValue()-((Number)state.get("server_time_ms")).longValue()<=300000);
        assertEquals(state.get("permission_version"),voice.get("permission_version"));
    }
    @Test void voiceAuthorityRejectsLegacyTokensAndExistingSessionAfterSignOut() {
        var device=client.device();var flow=client.scan(device);client.login(flow,null,null);
        String page=BindingTestClient.data(client.confirm(flow,"bind")).get("device_session_id").toString();
        var state=client.state(device);String token=((Map<?,?>)state.get("voice")).get("token").toString();
        assertEquals(401,voice("open",Map.of("device_id",device.id(),"token","legacy.signature")).getStatusCode().value());
        var opened=voice("open",Map.of("device_id",device.id(),"token",token));assertEquals(200,opened.getStatusCode().value());
        String session=opened.getBody().get("session").toString();
        assertEquals(200,voice("check",Map.of("session",session)).getStatusCode().value());
        var access=voice("model-access",Map.of("session",session));
        assertEquals(200,access.getStatusCode().value());
        assertEquals("qwen3.8-flash",access.getBody().get("model"));
        assertEquals("https://aimarket.hncig.cn/v1/",access.getBody().get("base_url"));
        assertFalse(client.page(flow).contains(access.getBody().get("api_key").toString()));
        client.post(flow,"device-sessions/"+page+"/sign-out","stop");
        assertEquals(403,voice("model-access",Map.of("session",session)).getStatusCode().value());
        assertEquals(403,voice("check",Map.of("session",session)).getStatusCode().value());
        assertEquals(403,voice("open",Map.of("device_id",device.id(),"token",token)).getStatusCode().value());
        assertNull(client.state(device).get("voice"));assertNull(client.state(device).get("lease"));
    }
    private org.springframework.http.ResponseEntity<Map> voice(String action,Map<String,Object> body) {
        var headers=new org.springframework.http.HttpHeaders();headers.set("X-Voice-Service-Key","isolated-voice-test-service-secret-123456");
        return http.postForEntity("/internal/device-voice/v1/"+action,new org.springframework.http.HttpEntity<>(body,headers),Map.class);
    }
    @Test void accessRefreshDoesNotSignOutAnOtherwiseValidModelGrant() throws Exception {
        var device=client.device();var flow=client.scan(device);var auth=client.login(flow,null,null);
        client.confirm(flow,"bind");
        var before=client.state(device);
        String oldToken=((Map<?,?>)before.get("voice")).get("token").toString();
        String session=voice("open",Map.of("device_id",device.id(),"token",oldToken)).getBody().get("session").toString();
        String refresh=market.refreshes.entrySet().stream().filter(e->auth.get("sub").equals(e.getValue().get("sub"))).findFirst().orElseThrow().getKey();
        var gates=new java.util.concurrent.CountDownLatch[]{new java.util.concurrent.CountDownLatch(1),new java.util.concurrent.CountDownLatch(1)};
        market.refreshGates.put(refresh,gates);
        try {
            CustomerTestTime.offset.set(301000);
            assertTrue(gates[0].await(5,java.util.concurrent.TimeUnit.SECONDS));
            assertEquals("allowed",client.state(device).get("use_permission"));
            assertEquals(before.get("permission_version"),client.state(device).get("permission_version"));
            assertEquals(401,voice("open",Map.of("device_id",device.id(),"token",oldToken)).getStatusCode().value());
            assertEquals(200,voice("check",Map.of("session",session)).getStatusCode().value());
            assertNotEquals(oldToken,((Map<?,?>)client.state(device).get("voice")).get("token"));
        } finally {gates[1].countDown();}
    }
    @org.junit.jupiter.params.ParameterizedTest
    @org.junit.jupiter.params.provider.ValueSource(strings={"invalid","lost"})
    void sharedGrantFailureStopsEnabledDevicesButKeepsSignedOutDeviceSignedOut(String mode) throws Exception {
        String subject=java.util.UUID.randomUUID().toString();
        var a=client.device();var b=client.device();var c=client.device();
        var fa=client.scan(a);client.login(fa,null,subject);client.confirm(fa,"bind");
        var fb=client.scan(b);client.login(fb,null,subject);client.confirm(fb,"bind");
        var fc=client.scan(c);client.login(fc,null,subject);
        String page=BindingTestClient.data(client.confirm(fc,"bind")).get("device_session_id").toString();
        client.post(fc,"device-sessions/"+page+"/sign-out","stop");
        market.refreshModes.put(subject,mode);CustomerTestTime.offset.set(271000);
        String expected=mode.equals("lost")?"market_auth_pending":"market_auth_required";
        long deadline=System.nanoTime()+java.time.Duration.ofSeconds(8).toNanos();
        while(!expected.equals(client.state(a).get("reason")) && System.nanoTime()<deadline) Thread.sleep(50);
        assertEquals(expected,client.state(a).get("reason"));assertEquals(expected,client.state(b).get("reason"));
        assertEquals("user_signed_out",client.state(c).get("reason"));
        var resume=client.scan(client.request(a,"auto"));client.login(resume,null,subject);
        assertEquals(expected,client.state(a).get("reason"));
        assertEquals(200,client.confirm(resume,"resume").getStatusCode().value());
        assertEquals("allowed",client.state(a).get("use_permission"));
        assertEquals(expected,client.state(b).get("reason"));
        assertEquals("user_signed_out",client.state(c).get("reason"));
    }
    @Test void resumeRequiresOriginalOwnerAndCurrentGrantGeneration() {
        var device=client.device();var flow=client.scan(device);var auth=client.login(flow,null,null);
        String page=BindingTestClient.data(client.confirm(flow,"bind")).get("device_session_id").toString();
        client.post(flow,"device-sessions/"+page+"/sign-out","stop");
        var request=client.request(device,"auto");
        var stranger=client.scan(request);client.login(stranger,null,null);
        assertEquals(403,client.confirm(stranger,"resume").getStatusCode().value());
        var old=client.scan(request);client.login(old,null,auth.get("sub"));
        var fresh=client.scan(request);client.login(fresh,null,auth.get("sub"));
        assertEquals(409,client.confirm(old,"resume").getStatusCode().value());
        assertEquals("user_signed_out",client.state(device).get("reason"));
        assertEquals(200,client.confirm(fresh,"resume").getStatusCode().value());
    }
    @Test void productOtaOnlyDiscoversControlAndNeverIssuesLegacyVoiceCredentials() {
        var headers=new org.springframework.http.HttpHeaders();headers.set("Device-Id","02:11:22:33:44:55");
        var result=http.postForEntity("/ota/",new org.springframework.http.HttpEntity<>(Map.of("application",Map.of("version","1.0.0")),headers),Map.class);
        assertEquals(200,result.getStatusCode().value());
        assertNull(result.getBody().get("websocket"));assertNull(result.getBody().get("activation"));
        assertEquals("https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/",((Map<?,?>)result.getBody().get("device_control")).get("base_url"));
        assertEquals(403,http.postForEntity("/ota/activate",new org.springframework.http.HttpEntity<>(Map.of(),headers),String.class).getStatusCode().value());
    }
    @Test void internalAuthorityRejectsMissingKeyMalformedAndUnsupportedBodies() {
        assertEquals(401,http.postForEntity("/internal/device-voice/v1/check",Map.of("session","unknown"),Map.class).getStatusCode().value());
        var headers=new org.springframework.http.HttpHeaders();headers.set("X-Voice-Service-Key","isolated-voice-test-service-secret-123456");
        headers.setContentType(org.springframework.http.MediaType.APPLICATION_JSON);
        for(String raw:java.util.List.of("{\"session\":\"a\",\"session\":\"b\"}","{} {}","[]","null"))
            assertEquals(400,http.postForEntity("/internal/device-voice/v1/check",new org.springframework.http.HttpEntity<>(raw,headers),Map.class).getStatusCode().value());
        headers.setContentType(org.springframework.http.MediaType.TEXT_PLAIN);
        assertEquals(400,http.postForEntity("/internal/device-voice/v1/check",new org.springframework.http.HttpEntity<>("{}",headers),Map.class).getStatusCode().value());
    }
}
