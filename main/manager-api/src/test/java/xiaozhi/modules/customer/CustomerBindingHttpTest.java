package xiaozhi.modules.customer;

import static org.junit.jupiter.api.Assertions.*;
import org.junit.jupiter.api.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.context.annotation.Import;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

@SpringBootTest(webEnvironment=SpringBootTest.WebEnvironment.RANDOM_PORT, properties={
        "server.servlet.context-path=/xiaozhi", "server.forward-headers-strategy=native",
        "renren.xss.enabled=true", "device-control.enabled=true",
        "device-control.public-origin=https://bridge.hncig.cn:18443"})
@Import(CustomerTestTime.class)
class CustomerBindingHttpTest {
    static final ControlledMarket market=new ControlledMarket();
    @DynamicPropertySource static void properties(DynamicPropertyRegistry registry) {
        registry.add("customer.oidc.config-file",()->market.config.toString());
        registry.add("customer.oidc.refresh-delay-ms",()->"200");
    }
    @Autowired TestRestTemplate http;
    @Autowired JdbcTemplate db;
    BindingTestClient client;
    @BeforeEach void setup() { client=new BindingTestClient(http,db,market); }
    @AfterEach void resetTime() { CustomerTestTime.offset.set(0); CustomerTestTime.frozen.set(0); }

    @Test void phoneConfirmationBindsOnceAndSafeRetryReturnsTheOriginalResult() {
        var device=client.device(); var flow=client.scan(device); client.login(flow,null,null);
        assertEquals("unbound",client.state(device).get("binding_state"));
        var confirmed=client.confirm(flow,"confirm-one");
        assertEquals(200,confirmed.getStatusCode().value());
        assertEquals("OK",confirmed.getBody().get("code"));
        var data=BindingTestClient.data(confirmed);
        assertEquals(2,((Number)data.get("permission_version")).intValue());
        assertNotNull(data.get("device_session_id"));
        assertEquals("bound",client.state(device).get("binding_state"));
        assertEquals("blocked",client.state(device).get("use_permission"));
        assertNull(client.state(device).get("voice"));
        assertTrue(client.page(flow).contains("绑定成功"));
        assertEquals(confirmed.getBody(),client.confirm(flow,"confirm-one").getBody());
        assertEquals(409,client.confirm(flow,"different-operation").getStatusCode().value());
        assertEquals(2,((Number)client.state(device).get("permission_version")).intValue());
    }

    @org.junit.jupiter.params.ParameterizedTest
    @org.junit.jupiter.params.provider.ValueSource(strings={"key-denied","key-unavailable","no-refresh"})
    void trustedIdentityCanBindWithoutPretendingBusinessAuthorizationSucceeded(String fault) {
        var device=client.device(); var flow=client.scan(device); client.login(flow,fault,null);
        assertEquals("IDENTITY_VERIFIED",client.status(flow).get("status"));
        assertEquals("NONE",client.status(flow).get("authorization_status"));
        assertEquals(200,client.confirm(flow,"bind-without-key").getStatusCode().value());
        var state=client.state(device);
        assertEquals("bound",state.get("binding_state"));
        assertEquals("blocked",state.get("use_permission"));
        assertEquals("market_auth_required",state.get("reason"));
        assertNull(state.get("voice")); assertNull(state.get("lease"));
    }

    @org.junit.jupiter.params.ParameterizedTest
    @org.junit.jupiter.params.provider.ValueSource(strings={"expired","idle","revoked","disabled","version","replaced","csrf","origin","identity","other-browser"})
    void confirmationRejectsInvalidIdentityOrChangedDeviceWithoutBinding(String fault) {
        var device=client.device(); var flow=client.scan(device);
        client.login(flow,fault.equals("identity") ? "nonce" : null,null);
        int expected=409;
        switch(fault) {
            case "expired" -> { CustomerTestTime.offset.set(600_000); expected=410; }
            case "idle" -> { CustomerTestTime.offset.set(1_800_000); expected=401; }
            case "revoked" -> db.update("UPDATE dc_device SET credential_hash=NULL WHERE device_id=?",device.id());
            case "disabled" -> db.update("UPDATE dc_device SET admitted=false WHERE device_id=?",device.id());
            case "version" -> db.update("UPDATE dc_device SET permission_version=permission_version+1 WHERE device_id=?",device.id());
            case "replaced" -> db.update("UPDATE dc_request SET status='replaced' WHERE device_id=?",device.id());
            case "csrf" -> { flow.headers().remove("X-CSRF-Token"); expected=403; }
            case "origin" -> { flow.headers().setOrigin("https://other.example"); expected=403; }
            case "identity" -> expected=401;
            case "other-browser" -> {
                var other=client.scan(device); flow=new BindingTestClient.Flow(other.headers(),flow.id(),flow.url()); expected=404;
            }
        }
        var rejected=client.confirm(flow,"rejected");
        assertEquals(expected,rejected.getStatusCode().value());
        assertNotEquals("OK",rejected.getBody().get("code"));
        CustomerTestTime.offset.set(0);
        if (fault.equals("revoked")) db.update("UPDATE dc_device SET credential_hash=? WHERE device_id=?",CustomerSecrets.hash(device.credential()),device.id());
        if (fault.equals("disabled")) db.update("UPDATE dc_device SET admitted=true WHERE device_id=?",device.id());
        assertEquals("unbound",client.state(device).get("binding_state"));
    }

    @Test void twoAccountsConfirmingConcurrentlyCannotOverwriteTheWinner() throws Exception {
        var device=client.device(); var first=client.scan(device); var second=client.scan(device);
        client.login(first,null,null); client.login(second,null,null);
        var gate=new java.util.concurrent.CountDownLatch(1);
        var one=java.util.concurrent.CompletableFuture.supplyAsync(()->{ await(gate); return client.confirm(first,"first"); });
        var two=java.util.concurrent.CompletableFuture.supplyAsync(()->{ await(gate); return client.confirm(second,"second"); });
        gate.countDown();
        var a=one.get(10,java.util.concurrent.TimeUnit.SECONDS); var b=two.get(10,java.util.concurrent.TimeUnit.SECONDS);
        assertEquals(java.util.Set.of(200,409),java.util.Set.of(a.getStatusCode().value(),b.getStatusCode().value()));
        var winner=a.getStatusCode().value()==200 ? first : second;
        var loser=winner==first ? second : first;
        assertEquals("DEVICE_ALREADY_BOUND",(winner==first ? b : a).getBody().get("code"));
        assertTrue(client.page(winner).contains("绑定成功"));
        assertFalse(client.page(loser).contains("绑定成功"));
        assertEquals("bound",client.state(device).get("binding_state"));
        assertEquals(2,((Number)client.state(device).get("permission_version")).intValue());
    }

    @Test void anotherLoginWithoutBusinessCredentialsDoesNotDiscardTheSharedGrant() {
        String subject=java.util.UUID.randomUUID().toString();
        var first=client.scan(client.device()); client.login(first,null,subject);
        assertEquals(200,client.confirm(first,"first-device").getStatusCode().value());
        var second=client.scan(client.device()); client.login(second,"key-unavailable",subject);
        assertEquals("NONE",client.status(second).get("authorization_status"));
        assertEquals("ACTIVE",client.status(first).get("authorization_status"));
        assertEquals(200,client.confirm(second,"second-device").getStatusCode().value());
        assertTrue(client.page(first).contains("绑定成功"));
        assertTrue(client.page(second).contains("绑定成功"));
    }

    @Test void logoutEndsConfirmationAuthorityButDoesNotUndoAnExistingBinding() {
        var device=client.device(); var flow=client.scan(device); var auth=client.login(flow,null,null);
        assertEquals(200,client.confirm(flow,"bound-before-logout").getStatusCode().value());
        var headers=new org.springframework.http.HttpHeaders(); headers.setContentType(org.springframework.http.MediaType.APPLICATION_FORM_URLENCODED);
        var body=new org.springframework.util.LinkedMultiValueMap<String,String>(); body.add("logout_token",market.logout(java.util.Map.of("sid",auth.get("sid"))));
        assertEquals(204,http.postForEntity("/customer/oidc/backchannel-logout",new org.springframework.http.HttpEntity<>(body,headers),String.class).getStatusCode().value());
        assertEquals(401,client.confirm(flow,"bound-before-logout").getStatusCode().value());
        assertEquals("bound",client.state(device).get("binding_state"));
    }

    @Test void ownerCanSignOutOnlyThisDeviceAndObserveItsStopAcknowledgement() {
        var a=client.device();var b=client.device();
        var first=client.scan(a);var second=client.scan(b);
        String subject=java.util.UUID.randomUUID().toString();
        client.login(first,null,subject);client.login(second,null,subject);
        String session=BindingTestClient.data(client.confirm(first,"bind-a")).get("device_session_id").toString();
        client.confirm(second,"bind-b");
        var headers=new org.springframework.http.HttpHeaders();headers.putAll(first.headers());headers.set("Idempotency-Key","sign-out-a");
        String path="/customer/v1/device-sessions/"+session+"/sign-out";
        var signedOut=http.postForEntity(path,new org.springframework.http.HttpEntity<>(java.util.Map.of(),headers),java.util.Map.class);
        assertEquals(200,signedOut.getStatusCode().value());
        var result=BindingTestClient.data(signedOut);
        assertEquals("stored",result.get("execution"));
        assertEquals(3,((Number)result.get("permission_version")).intValue());
        assertEquals("user_signed_out",client.state(a).get("reason"));
        assertEquals("bound",client.state(a).get("binding_state"));
        assertEquals("device_disabled",client.state(b).get("reason"));
        assertEquals("ACTIVE",client.status(second).get("authorization_status"));
        assertEquals(signedOut.getBody(),http.postForEntity(path,new org.springframework.http.HttpEntity<>(java.util.Map.of(),headers),java.util.Map.class).getBody());
        String operation="/customer/v1/device-operations/"+result.get("operation_id");
        assertEquals("stored",BindingTestClient.data(http.exchange(operation,org.springframework.http.HttpMethod.GET,new org.springframework.http.HttpEntity<>(first.headers()),java.util.Map.class)).get("execution"));
        var state=client.state(a);
        assertEquals(200,http.postForEntity("/device-control/v1/state-acks",new org.springframework.http.HttpEntity<>(java.util.Map.of("revision",state.get("revision"),"permission_version",3,"audio_stopped",true),BindingTestClient.deviceHeaders(a)),java.util.Map.class).getStatusCode().value());
        assertEquals("device_acknowledged",BindingTestClient.data(http.exchange(operation,org.springframework.http.HttpMethod.GET,new org.springframework.http.HttpEntity<>(first.headers()),java.util.Map.class)).get("execution"));
    }
    @Test void accountEntryRequiresTheOwnerAndConsumesRequestWithoutChangingPermission() {
        var device=client.device();var bound=client.scan(device);String subject=java.util.UUID.randomUUID().toString();
        client.login(bound,"no-refresh",subject);client.confirm(bound,"bind");
        var request=client.request(device,"account");
        var other=client.scan(request);client.login(other,"no-refresh",null);
        var rejected=client.post(other,"binding-flows/"+other.id()+"/open-account","open");
        assertEquals(403,rejected.getStatusCode().value());
        assertEquals("OWNER_REQUIRED",rejected.getBody().get("code"));
        var owner=client.scan(request);client.login(owner,"no-refresh",subject);
        assertEquals(409,client.confirm(owner,"wrong-purpose").getStatusCode().value());
        var opened=client.post(owner,"binding-flows/"+owner.id()+"/open-account","open");
        assertEquals(200,opened.getStatusCode().value());
        assertNotNull(BindingTestClient.data(opened).get("device_session_id"));
        assertEquals(2,((Number)client.state(device).get("permission_version")).intValue());
        assertEquals("market_auth_required",client.state(device).get("reason"));
        assertNull(client.state(device).get("binding_request"));
        assertEquals(opened.getBody(),client.post(owner,"binding-flows/"+owner.id()+"/open-account","open").getBody());
        assertEquals(409,client.post(owner,"binding-flows/"+owner.id()+"/open-account","new-key").getStatusCode().value());
    }
    @Test void phonePageOffersExplicitSignOutAndReportsStoredUntilDeviceAcknowledges() throws Exception {
        var device=client.device();var flow=client.scan(device);client.login(flow,null,null);
        String session=BindingTestClient.data(client.confirm(flow,"bind")).get("device_session_id").toString();
        assertTrue(client.page(flow).contains("账号与绑定"));
        String path="/customer/devices/"+session;
        var page=http.exchange(path,org.springframework.http.HttpMethod.GET,new org.springframework.http.HttpEntity<>(flow.headers()),String.class);
        assertTrue(page.getBody().contains("退出本台音箱"));
        var headers=new org.springframework.http.HttpHeaders();headers.putAll(flow.headers());headers.setContentType(org.springframework.http.MediaType.APPLICATION_FORM_URLENCODED);
        var body=new org.springframework.util.LinkedMultiValueMap<String,String>();body.add("csrf",flow.headers().getFirst("X-CSRF-Token"));body.add("idempotency_key",session);
        var request=java.net.http.HttpRequest.newBuilder(java.net.URI.create(http.getRootUri()+"/customer/v1/device-sessions/"+session+"/sign-out"))
                .header("Cookie",flow.headers().getFirst("Cookie")).header("Origin",flow.headers().getOrigin())
                .header("Content-Type","application/x-www-form-urlencoded")
                .POST(java.net.http.HttpRequest.BodyPublishers.ofString("csrf="+flow.headers().getFirst("X-CSRF-Token")+"&idempotency_key="+session)).build();
        var sent=java.net.http.HttpClient.newHttpClient().send(request,java.net.http.HttpResponse.BodyHandlers.ofString());
        assertEquals(303,sent.statusCode());
        assertEquals("/xiaozhi"+path,sent.headers().firstValue("Location").orElseThrow());
        var result=http.exchange(path,org.springframework.http.HttpMethod.GET,new org.springframework.http.HttpEntity<>(flow.headers()),String.class);
        assertTrue(result.getBody().contains("退出已保存，等待音箱确认停播"));
        assertFalse(result.getBody().contains("<button"));
    }
    @org.junit.jupiter.params.ParameterizedTest
    @org.junit.jupiter.params.provider.ValueSource(strings={"idle","csrf","origin","browser","logout","revoked","disabled","owner","version"})
    void deviceSessionCannotBypassIdentityOwnershipCredentialOrVersionChanges(String fault) {
        var device=client.device();var flow=client.scan(device);var auth=client.login(flow,null,null);
        String session=BindingTestClient.data(client.confirm(flow,"bind")).get("device_session_id").toString();
        int status=409;
        switch(fault) {
            case "idle" -> {
                CustomerTestTime.offset.set(1_799_000);
                assertEquals(200,http.exchange("/customer/devices/"+session,org.springframework.http.HttpMethod.GET,new org.springframework.http.HttpEntity<>(flow.headers()),String.class).getStatusCode().value());
                CustomerTestTime.offset.set(1_800_000);status=401;
            }
            case "csrf" -> { flow.headers().remove("X-CSRF-Token");status=403; }
            case "origin" -> { flow.headers().setOrigin("https://other.example");status=403; }
            case "browser" -> { var other=client.scan(client.device());flow=new BindingTestClient.Flow(other.headers(),flow.id(),flow.url());status=404; }
            case "logout" -> {
                var headers=new org.springframework.http.HttpHeaders();headers.setContentType(org.springframework.http.MediaType.APPLICATION_FORM_URLENCODED);
                var body=new org.springframework.util.LinkedMultiValueMap<String,String>();body.add("logout_token",market.logout(java.util.Map.of("sid",auth.get("sid"))));
                assertEquals(204,http.postForEntity("/customer/oidc/backchannel-logout",new org.springframework.http.HttpEntity<>(body,headers),String.class).getStatusCode().value());status=401;
            }
            case "revoked" -> db.update("UPDATE dc_device SET credential_hash=NULL WHERE device_id=?",device.id());
            case "disabled" -> db.update("UPDATE dc_device SET admitted=false WHERE device_id=?",device.id());
            case "owner" -> { db.update("UPDATE dc_device SET owner_subject='new-owner',ownership_version=ownership_version+1 WHERE device_id=?",device.id());status=403; }
            case "version" -> db.update("UPDATE dc_device SET permission_version=permission_version+1 WHERE device_id=?",device.id());
        }
        assertEquals(status,client.post(flow,"device-sessions/"+session+"/sign-out","invalid").getStatusCode().value());
        CustomerTestTime.offset.set(0);
        if(fault.equals("revoked")) db.update("UPDATE dc_device SET credential_hash=? WHERE device_id=?",CustomerSecrets.hash(device.credential()),device.id());
        if(fault.equals("disabled")) db.update("UPDATE dc_device SET admitted=true WHERE device_id=?",device.id());
        assertEquals("device_disabled",client.state(device).get("reason"));
    }

    @Test void signOutReplayAndOldStopAckCannotOverwriteOrConfirmALaterPermission() {
        var device=client.device();var flow=client.scan(device);client.login(flow,"no-refresh",null);
        String session=BindingTestClient.data(client.confirm(flow,"bind")).get("device_session_id").toString();
        String path="device-sessions/"+session+"/sign-out";
        var signedOut=client.post(flow,path,"stop");assertEquals(200,signedOut.getStatusCode().value());
        String operation="device-operations/"+BindingTestClient.data(signedOut).get("operation_id");
        var snapshot=client.state(device);
        var ack=java.util.Map.of("revision",snapshot.get("revision"),"permission_version",3,"audio_stopped",true);
        assertEquals(200,http.postForEntity("/device-control/v1/state-acks",new org.springframework.http.HttpEntity<>(ack,BindingTestClient.deviceHeaders(device)),java.util.Map.class).getStatusCode().value());
        // Future permission-change fixture: stale page must not stop a newer generation.
        db.update("UPDATE dc_device SET permission_version=permission_version+1,revision=revision+1,reason='market_auth_required' WHERE device_id=?",device.id());
        assertEquals(signedOut.getBody(),client.post(flow,path,"stop").getBody());
        assertEquals(409,client.post(flow,path,"new-stop").getStatusCode().value());
        assertEquals("superseded",BindingTestClient.data(client.get(flow,operation)).get("execution"));
        http.postForEntity("/device-control/v1/state-acks",new org.springframework.http.HttpEntity<>(ack,BindingTestClient.deviceHeaders(device)),java.util.Map.class);
        assertEquals("superseded",BindingTestClient.data(client.get(flow,operation)).get("execution"));
        assertEquals("market_auth_required",client.state(device).get("reason"));
        var stranger=client.scan(client.device());client.login(stranger,null,null);
        assertEquals(404,client.get(stranger,operation).getStatusCode().value());
        CustomerTestTime.offset.set(1_799_000);assertEquals(200,client.get(flow,operation).getStatusCode().value());
        CustomerTestTime.offset.set(1_801_000);assertEquals(401,client.get(flow,operation).getStatusCode().value());
    }

    @Test void failedSignOutReceiptRollsBackAndConcurrentRetryHasOneVersion() throws Exception {
        var device=client.device();var flow=client.scan(device);client.login(flow,null,null);
        String session=BindingTestClient.data(client.confirm(flow,"bind")).get("device_session_id").toString();
        assertTrue(session.matches("[A-Za-z0-9_-]{43}"));
        String path="device-sessions/"+session+"/sign-out";
        db.execute("CREATE TRIGGER issue13_fail_signout BEFORE INSERT ON customer_device_operation FOR EACH ROW BEGIN IF NEW.device_session_id='"+session+"' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='controlled operation save failure'; END IF; END");
        try {
            assertEquals(503,client.post(flow,path,"stop").getStatusCode().value());
            assertEquals("device_disabled",client.state(device).get("reason"));
            assertEquals(2,((Number)client.state(device).get("permission_version")).intValue());
        } finally { db.execute("DROP TRIGGER issue13_fail_signout"); }
        var gate=new java.util.concurrent.CountDownLatch(1);
        var one=java.util.concurrent.CompletableFuture.supplyAsync(()->{await(gate);return client.post(flow,path,"stop");});
        var two=java.util.concurrent.CompletableFuture.supplyAsync(()->{await(gate);return client.post(flow,path,"stop");});
        gate.countDown();var a=one.get(10,java.util.concurrent.TimeUnit.SECONDS);var b=two.get(10,java.util.concurrent.TimeUnit.SECONDS);
        assertEquals(200,a.getStatusCode().value());assertEquals(a.getBody(),b.getBody());
        assertEquals(3,((Number)client.state(device).get("permission_version")).intValue());
    }

    @org.junit.jupiter.params.ParameterizedTest
    @org.junit.jupiter.params.provider.ValueSource(strings={"expired","replaced","version","credential"})
    void accountEntryRechecksTheRequestAtFinalSubmission(String fault) {
        var device=client.device();var flow=client.scan(device);String subject=java.util.UUID.randomUUID().toString();
        client.login(flow,"no-refresh",subject);client.confirm(flow,"bind");
        var request=client.request(device,"account");var account=client.scan(request);client.login(account,"no-refresh",subject);
        int expected=409;
        switch(fault) {
            case "expired" -> {CustomerTestTime.offset.set(600_000);expected=410;}
            case "replaced" -> client.request(device,"auto");
            case "version" -> db.update("UPDATE dc_device SET permission_version=permission_version+1 WHERE device_id=?",device.id());
            case "credential" -> db.update("UPDATE dc_device SET credential_hash=NULL WHERE device_id=?",device.id());
        }
        assertEquals(expected,client.post(account,"binding-flows/"+account.id()+"/open-account","open").getStatusCode().value());
    }
    private static void await(java.util.concurrent.CountDownLatch gate) {
        try { gate.await(); } catch (InterruptedException e) { Thread.currentThread().interrupt(); throw new RuntimeException(e); }
    }

    @Test void confirmationRejectsClientSuppliedTargetAndUnsupportedMedia() {
        var flow=client.scan(client.device()); client.login(flow,null,null);
        var headers=new org.springframework.http.HttpHeaders();headers.putAll(flow.headers());headers.set("Idempotency-Key","target");
        var extra=http.postForEntity("/customer/v1/binding-flows/"+flow.id()+"/confirm",
                new org.springframework.http.HttpEntity<>(java.util.Map.of("device_id","another-device"),headers),java.util.Map.class);
        assertEquals(400,extra.getStatusCode().value());
        headers.setContentType(org.springframework.http.MediaType.TEXT_PLAIN);
        var wrongMedia=http.postForEntity("/customer/v1/binding-flows/"+flow.id()+"/confirm",
                new org.springframework.http.HttpEntity<>("{}",headers),java.util.Map.class);
        assertEquals(400,wrongMedia.getStatusCode().value());
        assertEquals("INVALID_REQUEST",wrongMedia.getBody().get("code"));
    }

    @org.junit.jupiter.params.ParameterizedTest
    @org.junit.jupiter.params.provider.ValueSource(strings={"confirm","open-account","sign-out"})
    void deviceActionsRejectNonEmptyOrMalformedJsonWithoutChangingPermission(String action) {
        var device=client.device();var flow=client.scan(device);client.login(flow,null,null);
        String session=BindingTestClient.data(client.confirm(flow,"bind")).get("device_session_id").toString();
        String path="sign-out".equals(action) ? "device-sessions/"+session+"/sign-out"
                : "binding-flows/"+flow.id()+"/"+action;
        var headers=new org.springframework.http.HttpHeaders();headers.putAll(flow.headers());
        headers.setContentType(org.springframework.http.MediaType.APPLICATION_JSON);
        headers.set("Idempotency-Key","invalid-body");
        var before=client.state(device);
        for(String raw:new String[]{"null","[]","1","\"text\"","{\"device_id\":\"another-device\"}","{} {}","{"}) {
            var response=http.postForEntity("/customer/v1/"+path,
                    new org.springframework.http.HttpEntity<>(raw,headers),java.util.Map.class);
            assertEquals(400,response.getStatusCode().value(),action+": "+raw);
            assertEquals("INVALID_REQUEST",response.getBody().get("code"));
            assertEquals(1,response.getBody().get("version"));
        }
        var after=client.state(device);
        assertEquals(before.get("permission_version"),after.get("permission_version"));
        assertEquals(before.get("revision"),after.get("revision"));
        assertEquals(before.get("reason"),after.get("reason"));
    }

    @Test void failedReceiptSaveRollsBackOwnershipAndTheSameRequestCanRetry() {
        var device=client.device();var flow=client.scan(device);client.login(flow,null,null);
        assertTrue(flow.id().matches("[A-Za-z0-9_-]{43}"));
        db.execute("CREATE TRIGGER issue13_fail_binding BEFORE INSERT ON customer_binding_operation FOR EACH ROW BEGIN IF NEW.flow_id='"
                +flow.id()+"' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='controlled binding save failure'; END IF; END");
        try {
            assertEquals(503,client.confirm(flow,"retry-after-save-failure").getStatusCode().value());
            assertEquals("unbound",client.state(device).get("binding_state"));
            assertEquals(1,((Number)client.state(device).get("permission_version")).intValue());
        } finally { db.execute("DROP TRIGGER issue13_fail_binding"); }
        assertEquals(200,client.confirm(flow,"retry-after-save-failure").getStatusCode().value());
        assertEquals("bound",client.state(device).get("binding_state"));
    }

    @Test void historicalConfirmationCannotUndoLaterPermissionOrOwnershipChanges() {
        var device=client.device();var flow=client.scan(device);client.login(flow,null,null);
        var saved=client.confirm(flow,"original");
        assertEquals(200,saved.getStatusCode().value());
        // Fixture for the future sign-out/transfer interfaces; confirm must not restore either.
        db.update("UPDATE dc_device SET reason='user_signed_out',permission_version=permission_version+1,revision=revision+1 WHERE device_id=?",device.id());
        assertEquals(saved.getBody(),client.confirm(flow,"original").getBody());
        assertEquals("user_signed_out",client.state(device).get("reason"));
        assertEquals(3,((Number)client.state(device).get("permission_version")).intValue());
        db.update("UPDATE dc_device SET owner_subject='replacement-owner',ownership_version=ownership_version+1 WHERE device_id=?",device.id());
        assertEquals(409,client.confirm(flow,"original").getStatusCode().value());
        assertFalse(client.page(flow).contains("绑定成功"));
    }

    @Test void uncertainCurrentGrantBindsWithPendingReasonAndNoVoiceLease() throws Exception {
        var device=client.device();var flow=client.scan(device);var auth=client.login(flow,null,null);
        market.refreshModes.put(auth.get("sub"),"lost");
        CustomerTestTime.offset.set(271_000);
        long deadline=System.nanoTime()+java.time.Duration.ofSeconds(8).toNanos();
        while (!"REFRESH_UNCERTAIN".equals(client.status(flow).get("authorization_status")) && System.nanoTime()<deadline) Thread.sleep(50);
        assertEquals("REFRESH_UNCERTAIN",client.status(flow).get("authorization_status"));
        assertEquals(200,client.confirm(flow,"pending").getStatusCode().value());
        assertEquals("market_auth_pending",client.state(device).get("reason"));
        assertNull(client.state(device).get("voice"));
        assertNull(client.state(device).get("lease"));
    }
}
