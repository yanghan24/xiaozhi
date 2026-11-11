package xiaozhi.modules.customer;

import static org.junit.jupiter.api.Assertions.*;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.context.annotation.Import;
import org.springframework.http.HttpEntity;
import org.springframework.http.HttpMethod;
import org.springframework.http.ResponseEntity;
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
        registry.add("customer.oidc.balance-enabled", () -> "true");
    }
    @Autowired TestRestTemplate http;
    @Autowired JdbcTemplate db;
    @org.springframework.boot.test.mock.mockito.MockBean(name="deviceControlClock") java.time.Clock deviceClock;
    @Autowired @org.springframework.beans.factory.annotation.Qualifier("customerTime") java.time.Clock customerClock;
    BindingTestClient client;
    @Autowired org.springframework.transaction.PlatformTransactionManager transactionManager;
    @Autowired CustomerOidcConfig oidcConfig;
    @BeforeEach void setup(){client=new BindingTestClient(http,db,market);org.mockito.Mockito.when(deviceClock.millis()).thenAnswer(call->customerClock.millis());}
    @AfterEach void reset(){CustomerTestTime.offset.set(0);CustomerTestTime.frozen.set(0);}
    @Test void deviceReadsOnlyItsCurrentOwnersLiveMarketAccount() {
        var device = client.device();
        assertEquals(409, account(device).getStatusCode().value());
        var flow = client.scan(device);
        var owner = UUID.randomUUID().toString();
        assertTrue(client.login(flow, null, owner).get("scope").contains("balance:read"));
        assertEquals(200, client.confirm(flow, "bind-account").getStatusCode().value());
        var response = account(device);
        assertEquals(200, response.getStatusCode().value());
        assertEquals("no-store", response.getHeaders().getCacheControl());
        var data = BindingTestClient.data(response);
        assertEquals(Map.of("device_id", device.id(), "permission_version", 2,
                "username", "market-" + owner, "balance", "-12.75"), data);
        market.balances.put(owner, new java.math.BigDecimal("900719925474099.125"));
        assertEquals("900719925474099.125", BindingTestClient.data(account(device)).get("balance"));

        var other = client.device();
        var wrongCredential = new BindingTestClient.Device(device.id(), other.credential(), null);
        assertEquals(401, account(wrongCredential).getStatusCode().value());
        assertEquals(401, http.getForEntity("/device-control/v1/account", Map.class).getStatusCode().value());
    }

    @org.junit.jupiter.params.ParameterizedTest
    @org.junit.jupiter.params.provider.ValueSource(strings = {"no-balance", "balance-mismatch"})
    void unavailableBalanceDoesNotInventPointsOrStopVoice(String fault) {
        var device = client.device();
        var flow = client.scan(device);
        var owner = UUID.randomUUID().toString();
        client.login(flow, fault, owner);
        assertEquals(200, client.confirm(flow, "bind-account").getStatusCode().value());
        var details = BindingTestClient.data(account(device));
        assertEquals("market-" + owner, details.get("username"));
        assertTrue(details.containsKey("balance"));
        assertNull(details.get("balance"));
        assertEquals("allowed", client.state(device).get("use_permission"));
    }

    @Test void speakerNameMatchesTheMarketDisplayAndRefreshesLive() {
        var device = client.device();
        var flow = client.scan(device);
        var owner = UUID.randomUUID().toString();
        market.profiles.put(owner, Map.of("preferred_username", "wx_generated123",
                "name", "集市名称", "nickname", "集市昵称"));
        client.login(flow, null, owner);
        assertEquals(200, client.confirm(flow, "bind-display-name").getStatusCode().value());
        assertEquals("集市昵称", BindingTestClient.data(account(device)).get("username"));
        market.profiles.put(owner, Map.of("preferred_username", "wx_generated123", "name", "更新名称"));
        assertEquals("更新名称", BindingTestClient.data(account(device)).get("username"));
        market.profiles.put(owner, Map.of("preferred_username", "wx_generated123"));
        assertEquals("wx_generated123", BindingTestClient.data(account(device)).get("username"));
    }

    @Test void speakerSignOutStopsOnlyItAndCannotReplayAfterRecovery() {
        var owner = UUID.randomUUID().toString();
        var first = client.device();
        var second = client.device();
        for (var device : java.util.List.of(first, second)) {
            var flow = client.scan(device);
            client.login(flow, null, owner);
            assertEquals(200, client.confirm(flow, "bind").getStatusCode().value());
        }
        var firstState = client.state(first);
        var secondState = client.state(second);
        String token = ((Map<?, ?>) firstState.get("voice")).get("token").toString();
        var voiceSession = voice("open", Map.of("device_id", first.id(), "token", token));
        assertEquals(200, voiceSession.getStatusCode().value());
        long version = ((Number) firstState.get("permission_version")).longValue();
        var wrongCredential = new BindingTestClient.Device(first.id(), second.credential(), null);
        assertEquals(401, signOut(wrongCredential, version).getStatusCode().value());
        var stopped = signOut(first, version);
        assertEquals(200, stopped.getStatusCode().value());
        assertEquals(stopped.getBody(), signOut(first, version).getBody());
        var snapshot = client.state(first);
        assertEquals("bound", snapshot.get("binding_state"));
        assertEquals("user_signed_out", snapshot.get("reason"));
        assertNull(snapshot.get("voice"));
        assertEquals(version + 1, ((Number) snapshot.get("permission_version")).longValue());
        assertEquals(403, voice("check", Map.of("session", voiceSession.getBody().get("session"))).getStatusCode().value());
        assertEquals(409, account(first).getStatusCode().value());
        assertEquals(secondState.get("permission_version"), client.state(second).get("permission_version"));
        assertEquals("allowed", client.state(second).get("use_permission"));
        assertEquals("market-" + owner, BindingTestClient.data(account(second)).get("username"));

        var ack = Map.of("revision", snapshot.get("revision"),
                "permission_version", snapshot.get("permission_version"), "audio_stopped", true);
        var headers = BindingTestClient.deviceHeaders(first);
        assertEquals(200, http.postForEntity("/device-control/v1/state-acks",
                new HttpEntity<>(ack, headers), Map.class).getStatusCode().value());
        var resume = client.scan(client.request(first, "auto"));
        client.login(resume, null, owner);
        assertEquals(200, client.confirm(resume, "resume").getStatusCode().value());
        assertEquals(409, signOut(first, version).getStatusCode().value());
        assertEquals("allowed", client.state(first).get("use_permission"));
        assertEquals(version + 2, ((Number) client.state(first).get("permission_version")).longValue());
    }

    @org.junit.jupiter.params.ParameterizedTest
    @org.junit.jupiter.params.provider.CsvSource({"false,false", "false,true", "true,false", "true,true"})
    void speakerUnbindAllowsAnotherAccountAndFencesEveryOldOwnerAction(boolean sameAccount,
            boolean previouslySignedOut) {
        var owner = UUID.randomUUID().toString();
        var first = client.device();
        var second = client.device();
        var oldFlow = client.scan(first);
        client.login(oldFlow, null, owner);
        var oldSession = BindingTestClient.data(client.confirm(oldFlow, "bind-original"))
                .get("device_session_id").toString();
        var peerFlow = client.scan(second);
        client.login(peerFlow, null, owner);
        assertEquals(200, client.confirm(peerFlow, "bind-peer").getStatusCode().value());
        var original = client.state(first);
        var peer = client.state(second);
        var voiceSession = voice("open", Map.of("device_id", first.id(),
                "token", ((Map<?, ?>) original.get("voice")).get("token")));
        assertEquals(200, voiceSession.getStatusCode().value());
        BindingTestClient.Flow oldResume = null;
        if (previouslySignedOut) {
            assertEquals(200, signOut(first, ((Number) original.get("permission_version")).longValue())
                    .getStatusCode().value());
            oldResume = client.scan(client.request(first, "auto"));
            client.login(oldResume, null, owner);
            original = client.state(first);
        }
        long version = ((Number) original.get("permission_version")).longValue();

        var wrongCredential = new BindingTestClient.Device(first.id(), second.credential(), null);
        assertEquals(401, unbind(wrongCredential, version).getStatusCode().value());
        assertEquals(409, unbind(first, version - 1).getStatusCode().value());
        var released = unbind(first, version);
        assertEquals(200, released.getStatusCode().value());
        assertEquals("no-store", released.getHeaders().getCacheControl());
        assertEquals(Map.of("permission_version", (int) version + 1, "binding_state", "unbound"),
                BindingTestClient.data(released));
        assertEquals(released.getBody(), unbind(first, version).getBody());
        var waiting = client.state(first);
        assertEquals("unbound", waiting.get("binding_state"));
        assertEquals("unbound", waiting.get("reason"));
        assertEquals("blocked", waiting.get("use_permission"));
        assertNull(waiting.get("voice"));
        assertEquals(403, voice("check", Map.of("session", voiceSession.getBody().get("session")))
                .getStatusCode().value());
        assertEquals(409, account(first).getStatusCode().value());

        var next = client.request(first, "auto");
        assertEquals("bind", ((Map<?, ?>) client.state(first).get("binding_request")).get("purpose"));
        var nextFlow = client.scan(next);
        var nextOwner = sameAccount ? owner : UUID.randomUUID().toString();
        client.login(nextFlow, null, nextOwner);
        assertEquals(200, client.confirm(nextFlow, "bind-next-owner").getStatusCode().value());
        assertEquals("market-" + nextOwner, BindingTestClient.data(account(first)).get("username"));
        assertEquals(409, client.confirm(oldFlow, "bind-original").getStatusCode().value());
        assertEquals(403, client.post(oldFlow, "device-sessions/" + oldSession + "/sign-out", "late-stop")
                .getStatusCode().value());
        if (oldResume != null)
            assertEquals(sameAccount ? 409 : 403, client.confirm(oldResume, "late-resume").getStatusCode().value());
        assertEquals(409, unbind(first, version).getStatusCode().value());
        assertEquals("allowed", client.state(first).get("use_permission"));
        assertEquals(peer.get("permission_version"), client.state(second).get("permission_version"));
        assertEquals(peer.get("revision"), client.state(second).get("revision"));
        assertEquals("allowed", client.state(second).get("use_permission"));
        assertEquals("market-" + owner, BindingTestClient.data(account(second)).get("username"));
    }

    private ResponseEntity<Map> unbind(BindingTestClient.Device device, long version) {
        return deviceAction(device, "unbind", version);
    }

    @Test void accountLookupDoesNotBlockOrLeakAcrossUnbindAndNewOwner() throws Exception {
        var device = client.device();
        var flow = client.scan(device);
        var owner = UUID.randomUUID().toString();
        var nextOwner = UUID.randomUUID().toString();
        client.login(flow, null, owner);
        assertEquals(200, client.confirm(flow, "bind").getStatusCode().value());
        var gate = new CountDownLatch[] {new CountDownLatch(1), new CountDownLatch(1)};
        market.profileGates.put(owner, gate);
        var lookup = CompletableFuture.supplyAsync(() -> account(device));
        try {
            assertTrue(gate[0].await(4, TimeUnit.SECONDS));
            assertEquals(200, CompletableFuture.supplyAsync(() -> unbind(device, 2))
                    .get(3, TimeUnit.SECONDS).getStatusCode().value());
            var nextFlow = client.scan(client.request(device, "auto"));
            client.login(nextFlow, null, nextOwner);
            assertEquals(200, client.confirm(nextFlow, "bind-new-owner").getStatusCode().value());
        } finally {
            gate[1].countDown();
            market.profileGates.remove(owner);
        }
        var result = lookup.get(5, TimeUnit.SECONDS);
        assertEquals(409, result.getStatusCode().value());
        assertFalse(result.getBody().toString().contains(owner));
        assertEquals("market-" + nextOwner, BindingTestClient.data(account(device)).get("username"));
    }

    @Test void accountLookupDoesNotBlockOrLeakAcrossSignOut() throws Exception {
        var device = client.device();
        var flow = client.scan(device);
        var owner = UUID.randomUUID().toString();
        client.login(flow, null, owner);
        assertEquals(200, client.confirm(flow, "bind").getStatusCode().value());
        var gate = new CountDownLatch[] {new CountDownLatch(1), new CountDownLatch(1)};
        market.profileGates.put(owner, gate);
        var lookup = CompletableFuture.supplyAsync(() -> account(device));
        try {
            assertTrue(gate[0].await(4, TimeUnit.SECONDS));
            assertEquals(200, CompletableFuture.supplyAsync(() -> signOut(device, 2))
                    .get(3, TimeUnit.SECONDS).getStatusCode().value());
        } finally {
            gate[1].countDown();
            market.profileGates.remove(owner);
        }
        var result = lookup.get(5, TimeUnit.SECONDS);
        assertEquals(409, result.getStatusCode().value());
        assertFalse(result.getBody().toString().contains(owner));
    }

    private ResponseEntity<Map> account(BindingTestClient.Device device) {
        return http.exchange("/device-control/v1/account", HttpMethod.GET,
                new HttpEntity<>(BindingTestClient.deviceHeaders(device)), Map.class);
    }

    private ResponseEntity<Map> signOut(BindingTestClient.Device device, long version) {
        return deviceAction(device, "sign-out", version);
    }

    private ResponseEntity<Map> deviceAction(BindingTestClient.Device device, String action, long version) {
        var headers = BindingTestClient.deviceHeaders(device);
        headers.setContentType(org.springframework.http.MediaType.APPLICATION_JSON);
        // The legacy admin mapper writes Long IDs as strings; the device protocol needs a JSON number.
        return http.postForEntity("/device-control/v1/" + action,
                new HttpEntity<>("{\"expected_permission_version\":" + version + "}", headers), Map.class);
    }

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
    @Test void rejectedKeyStopsSharedDevicesAndOldGenerationCannotStopReplacement() {
        String subject=java.util.UUID.randomUUID().toString();
        var a=client.device();var b=client.device();
        var fa=client.scan(a);client.login(fa,null,subject);client.confirm(fa,"bind");
        var fb=client.scan(b);client.login(fb,null,subject);client.confirm(fb,"bind");
        String token=((Map<?,?>)client.state(a).get("voice")).get("token").toString();
        String session=voice("open",Map.of("device_id",a.id(),"token",token)).getBody().get("session").toString();
        String receipt=voice("model-access",Map.of("session",session)).getBody().get("rejection_receipt").toString();
        assertEquals(200,voice("close",Map.of("session",session)).getStatusCode().value());
        CustomerTestTime.offset.set(301000); // Delivery can resume after a long authority outage.
        assertEquals(200,voice("key-rejected",Map.of("rejection_receipt",receipt)).getStatusCode().value());
        assertEquals("market_auth_required",client.state(a).get("reason"));
        assertEquals("market_auth_required",client.state(b).get("reason"));
        assertEquals(401,voice("check",Map.of("session",session)).getStatusCode().value());
        var resume=client.scan(client.request(a,"auto"));client.login(resume,null,subject);
        assertEquals(200,client.confirm(resume,"resume").getStatusCode().value());
        assertEquals(200,voice("key-rejected",Map.of("rejection_receipt",receipt)).getStatusCode().value());
        assertEquals("allowed",client.state(a).get("use_permission"));
        assertEquals("market_auth_required",client.state(b).get("reason"));
        assertEquals(400,voice("key-rejected",Map.of("rejection_receipt","invalid")).getStatusCode().value());
        CustomerTestTime.offset.set(301000);
        assertEquals(200,voice("key-rejected",Map.of("rejection_receipt",receipt)).getStatusCode().value());
    }
    @org.junit.jupiter.params.ParameterizedTest
    @org.junit.jupiter.params.provider.ValueSource(strings={"invalid","lost"})
    void refreshFailureSharesReplacementBarrierAndStopsDevicesBeforeRelogin(String mode) throws Exception {
        String subject=java.util.UUID.randomUUID().toString();
        var a=client.device();var b=client.device();
        var fa=client.scan(a);client.login(fa,null,subject);client.confirm(fa,"bind");
        var fb=client.scan(b);client.login(fb,null,subject);client.confirm(fb,"bind");
        // Select the exact current rotating credential, not an earlier login's token.
        String refresh=oidcConfig.decrypt(db.queryForObject("SELECT refresh_token FROM customer_authorization WHERE subject=?",String.class,subject));
        var gates=new java.util.concurrent.CountDownLatch[]{new java.util.concurrent.CountDownLatch(1),new java.util.concurrent.CountDownLatch(1)};
        market.refreshGates.put(refresh,gates);market.refreshModes.put(subject,mode);
        try {
            CustomerTestTime.offset.set(271000);
            assertTrue(gates[0].await(5,java.util.concurrent.TimeUnit.SECONDS));
            new org.springframework.transaction.support.TransactionTemplate(transactionManager).executeWithoutResult(tx->{
                db.queryForObject("SELECT id FROM customer_identity_barrier WHERE id=1 FOR UPDATE",Integer.class);
                assertEquals("REFRESHING",db.queryForObject("SELECT status FROM customer_authorization WHERE subject=?",String.class,subject));
                gates[1].countDown();
                try {Thread.sleep(400);}catch(InterruptedException e){throw new RuntimeException(e);}
                // A locking read observes commits even under repeatable-read snapshots.
                assertEquals("REFRESHING",db.queryForObject("SELECT status FROM customer_authorization WHERE subject=? FOR UPDATE",String.class,subject));
            });
            String expected=mode.equals("lost")?"market_auth_pending":"market_auth_required";
            long deadline=System.nanoTime()+java.time.Duration.ofSeconds(6).toNanos();
            while(!expected.equals(client.state(a).get("reason"))&&System.nanoTime()<deadline)Thread.sleep(20);
            assertEquals(expected,client.state(b).get("reason"));
            var resume=client.scan(client.request(a,"auto"));client.login(resume,null,subject);
            assertEquals(expected,client.state(b).get("reason"));
            assertEquals(200,client.confirm(resume,"resume").getStatusCode().value());
            assertEquals("allowed",client.state(a).get("use_permission"));
            assertEquals(expected,client.state(b).get("reason"));
        } finally {gates[1].countDown();}
    }
    @Test void delayedRejectionAfterReloginStopsOldPermissionsButNotNewExplicitResume() {
        String subject=java.util.UUID.randomUUID().toString();
        var a=client.device();var b=client.device();
        var fa=client.scan(a);client.login(fa,null,subject);client.confirm(fa,"bind");
        var fb=client.scan(b);client.login(fb,null,subject);client.confirm(fb,"bind");
        String token=((Map<?,?>)client.state(a).get("voice")).get("token").toString();
        String session=voice("open",Map.of("device_id",a.id(),"token",token)).getBody().get("session").toString();
        var old=voice("model-access",Map.of("session",session)).getBody();
        var login=client.scan(client.request(a,"account"));client.login(login,null,subject);
        assertEquals("allowed",client.state(b).get("use_permission"));
        assertNotEquals(old.get("authorization_scope"),voice("model-access",Map.of("session",session)).getBody().get("authorization_scope"));
        assertEquals(200,voice("key-rejected",Map.of("rejection_receipt",old.get("rejection_receipt"))).getStatusCode().value());
        assertEquals("market_auth_required",client.state(a).get("reason"));
        assertEquals("market_auth_required",client.state(b).get("reason"));
        var resume=client.scan(client.request(a,"auto"));client.login(resume,null,subject);
        assertEquals(200,client.confirm(resume,"resume").getStatusCode().value());
        assertEquals(200,voice("key-rejected",Map.of("rejection_receipt",old.get("rejection_receipt"))).getStatusCode().value());
        assertEquals("allowed",client.state(a).get("use_permission"));
        assertEquals("market_auth_required",client.state(b).get("reason"));
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
