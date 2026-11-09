package xiaozhi.modules.customer;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.UUID;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpEntity;
import org.springframework.http.HttpMethod;
import java.util.Map;
import org.springframework.context.annotation.Import;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

/** Exercises the application's actual HTTP filters, not direct controller calls. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT,
        properties = {"server.servlet.context-path=/xiaozhi", "renren.xss.enabled=true"})
@Import(CustomerTestTime.class)
class CustomerIdentityHttpTest {
    @AfterEach void resetTime() { CustomerTestTime.offset.set(0); CustomerTestTime.frozen.set(0); }
    static final ControlledMarket market = new ControlledMarket();
    @DynamicPropertySource
    static void properties(DynamicPropertyRegistry registry) {
        registry.add("customer.oidc.config-file", () -> market.config.toString());
        registry.add("customer.oidc.refresh-delay-ms", () -> "200");
    }
    @Autowired TestRestTemplate http;
    @Autowired JdbcTemplate db;
    @org.springframework.boot.test.web.server.LocalServerPort int port;

    @Test
    void olderCallbackCannotReplaceNewerGrantEvenWhenFlowsStartInSameMillisecond() throws Exception {
        CustomerTestTime.frozen.set(System.currentTimeMillis());
        Flow old=start(), fresh=start();
        String oldCode=market.approve(old.url()), freshCode=market.approve(fresh.url());
        var oldAuth=market.codes.get(oldCode);var freshAuth=market.codes.get(freshCode);
        freshAuth.put("sub",oldAuth.get("sub"));
        http.exchange("/customer/oidc/callback?code="+freshCode+"&state="+freshAuth.get("state"),HttpMethod.GET,new HttpEntity<>(fresh.headers()),String.class);
        http.exchange("/customer/oidc/callback?code="+oldCode+"&state="+oldAuth.get("state"),HttpMethod.GET,new HttpEntity<>(old.headers()),String.class);
        String oldRefresh=market.refreshes.entrySet().stream().filter(e -> oldAuth.get("sid").equals(e.getValue().get("sid"))).findFirst().orElseThrow().getKey();
        String freshRefresh=market.refreshes.entrySet().stream().filter(e -> freshAuth.get("sid").equals(e.getValue().get("sid"))).findFirst().orElseThrow().getKey();
        CustomerTestTime.offset.set(271_000);
        long deadline=System.nanoTime()+java.time.Duration.ofSeconds(5).toNanos();
        while (!market.refreshAttempts.containsKey(freshRefresh) && System.nanoTime()<deadline) Thread.sleep(50);
        assertTrue(market.refreshAttempts.containsKey(freshRefresh));
        assertTrue(!market.refreshAttempts.containsKey(oldRefresh));
    }

    @Test
    void oldRefreshFailureCannotInvalidateNewAuthorization() throws Exception {
        Flow old=start(); var auth=finish(old);
        String original=market.refreshes.entrySet().stream().filter(e -> e.getValue().get("sub").equals(auth.get("sub"))).findFirst().orElseThrow().getKey();
        var gate=new java.util.concurrent.CountDownLatch[]{new java.util.concurrent.CountDownLatch(1),new java.util.concurrent.CountDownLatch(1)};
        market.refreshGates.put(original,gate); market.refreshModes.put(auth.get("sub"),"invalid");
        try {
            CustomerTestTime.offset.set(271_000);
            assertTrue(gate[0].await(5,java.util.concurrent.TimeUnit.SECONDS));
            Flow fresh=start(); String code=market.approve(fresh.url());
            var newAuth=market.codes.get(code);newAuth.put("sub",auth.get("sub"));
            http.exchange("/customer/oidc/callback?code="+code+"&state="+newAuth.get("state"),HttpMethod.GET,new HttpEntity<>(fresh.headers()),String.class);
            assertEquals("IDENTITY_VERIFIED",status(fresh).get("status"));
            gate[1].countDown(); Thread.sleep(500);
            assertEquals("ACTIVE",status(fresh).get("authorization_status"));
        } finally {gate[1].countDown();}
    }

    @Test
    void expiredFlowNoLongerAppearsToBeWaitingAndResultIdentifiesTheTargetDevice() {
        Flow flow=start();
        assertTrue(status(flow).get("device_label").toString().contains("Test speaker"));
        CustomerTestTime.offset.set(301_000);
        assertEquals("FLOW_EXPIRED",status(flow).get("status"));
    }

    @Test
    void logoutReceivedDuringLoginCannotBeUndoneByTheCallback() {
        Flow flow = start(); String code = market.approve(flow.url());
        var auth = market.codes.get(code);
        assertEquals(204, logout(market.logout(Map.of("sid", auth.get("sid")))));
        http.exchange("/customer/oidc/callback?code="+code+"&state="+auth.get("state"), HttpMethod.GET, new HttpEntity<>(flow.headers()), String.class);
        assertEquals("SESSION_ENDED", status(flow).get("status"));
    }

    @Test
    void failedRefreshPersistenceNeverReplaysTheConsumedRefreshToken() throws Exception {
        Flow flow = start(); var auth = finish(flow);
        String original = market.refreshes.entrySet().stream().filter(e -> e.getValue().get("sub").equals(auth.get("sub"))).findFirst().orElseThrow().getKey();
        // Real MySQL fixture fault; no internal repository mocks or database assertions.
        assertTrue(auth.get("sub").matches("[a-f0-9-]+"));
        db.execute("CREATE TRIGGER issue29_fail_refresh_save BEFORE UPDATE ON customer_authorization FOR EACH ROW BEGIN IF OLD.status='REFRESHING' AND NEW.status='ACTIVE' AND NEW.subject='"
                + auth.get("sub") + "' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='controlled persistence failure'; END IF; END");
        try {
            CustomerTestTime.offset.set(271_000);
            long deadline=System.nanoTime()+java.time.Duration.ofSeconds(8).toNanos();
            while (!"REAUTH_REQUIRED".equals(status(flow).get("authorization_status")) && System.nanoTime()<deadline) Thread.sleep(50);
            assertEquals("REAUTH_REQUIRED",status(flow).get("authorization_status"));
            Thread.sleep(500);
            assertEquals(1,market.refreshAttempts.get(original).get());
        } finally { db.execute("DROP TRIGGER issue29_fail_refresh_save"); }
    }

    @Test
    void rejectedCallbackAlsoRedirectsAwayFromTemporaryCode() throws Exception {
        var client = java.net.http.HttpClient.newBuilder().followRedirects(java.net.http.HttpClient.Redirect.NEVER).build();
        var response = client.send(java.net.http.HttpRequest.newBuilder(java.net.URI.create("http://127.0.0.1:" + port
                + "/xiaozhi/customer/oidc/callback?code=sensitive-temporary-code&state=unknown")).GET().build(), java.net.http.HttpResponse.BodyHandlers.ofString());
        assertEquals(303, response.statusCode());
        assertTrue(!response.headers().firstValue("Location").orElseThrow().contains("sensitive-temporary-code"));
    }

    @ParameterizedTest
    @ValueSource(strings={"invalid", "lost", "server"})
    void uncertainRefreshNeverReplaysAndOnlyConfirmedInvalidGrantRequiresReauthorization(String mode) throws Exception {
        Flow flow = start(); var auth = finish(flow);
        String original = market.refreshes.entrySet().stream().filter(e -> e.getValue().get("sub").equals(auth.get("sub"))).findFirst().orElseThrow().getKey();
        market.refreshModes.put(auth.get("sub"), mode);
        CustomerTestTime.offset.set(271_000);
        String expected = mode.equals("invalid") ? "REAUTH_REQUIRED" : "REFRESH_UNCERTAIN";
        long deadline = System.nanoTime() + java.time.Duration.ofSeconds(10).toNanos();
        while (!expected.equals(status(flow).get("authorization_status")) && System.nanoTime() < deadline) Thread.sleep(50);
        assertEquals(expected, status(flow).get("authorization_status"));
        Thread.sleep(600);
        assertEquals(1, market.refreshAttempts.get(original).get());
    }

    @ParameterizedTest
    @ValueSource(strings={"nonce", "issuer", "audience", "expired", "signature", "userinfo", "network"})
    void invalidIdentityOrUpstreamFailureCannotCreateIdentity(String fault) {
        Flow flow = start(); String code = market.approve(flow.url());
        var auth = market.codes.get(code); auth.put("fault", fault);
        http.exchange("/customer/oidc/callback?code=" + code + "&state=" + auth.get("state"), HttpMethod.GET, new HttpEntity<>(flow.headers()), String.class);
        assertEquals("IDENTITY_FAILED", status(flow).get("status"));
        assertEquals("NONE", status(flow).get("authorization_status"));
    }

    @Test
    void callbackCannotCrossBrowsersOrReplayAndRefusalIsClear() {
        Flow flow = start(), other = start(); String code = market.approve(flow.url());
        String state = market.codes.get(code).get("state");
        String url = "/customer/oidc/callback?code=" + code + "&state=" + state;
        assertEquals(400, http.exchange(url, HttpMethod.GET, new HttpEntity<>(other.headers()), String.class).getStatusCode().value());
        assertEquals(200, http.exchange(url, HttpMethod.GET, new HttpEntity<>(flow.headers()), String.class).getStatusCode().value());
        assertEquals(409, http.exchange(url, HttpMethod.GET, new HttpEntity<>(flow.headers()), String.class).getStatusCode().value());
        String otherState = ControlledMarket.params(java.net.URI.create(other.url()).getRawQuery()).get("state");
        var refused = http.exchange("/customer/oidc/callback?error=access_denied&state=" + otherState, HttpMethod.GET, new HttpEntity<>(other.headers()), String.class);
        assertTrue(refused.getBody().contains("已取消身份授权"));
        assertEquals("NONE", status(other).get("authorization_status"));
        var admin = http.exchange("/user/info", HttpMethod.GET, new HttpEntity<>(flow.headers()), String.class);
        assertTrue(!admin.getBody().contains("superAdmin"));
        assertTrue(admin.getBody().contains("401"));
    }

    @ParameterizedTest
    @ValueSource(strings={"nonce", "audience", "issuer", "expired", "future", "events", "missing-target", "signature"})
    void invalidLogoutDoesNotChangeAnyIdentity(String fault) {
        Flow flow = start(); var auth = finish(flow);
        var overrides = new java.util.HashMap<String,Object>(); overrides.put("sub", auth.get("sub"));
        switch (fault) {
            case "nonce" -> overrides.put("nonce", "forbidden");
            case "audience" -> overrides.put("aud", "wrong-client");
            case "issuer" -> overrides.put("iss", "https://wrong.example");
            case "expired" -> overrides.put("exp", System.currentTimeMillis() / 1000 - 100);
            case "future" -> overrides.put("iat", System.currentTimeMillis() / 1000 + 600);
            case "events" -> overrides.put("events", Map.of());
            case "missing-target" -> overrides.remove("sub");
            default -> { }
        }
        String token = market.logout(overrides);
        if (fault.equals("signature")) token = token.substring(0, token.lastIndexOf('.') + 1) + "invalid";
        assertEquals(400, logout(token));
        assertEquals("IDENTITY_VERIFIED", status(flow).get("status"));
        assertEquals("ACTIVE", status(flow).get("authorization_status"));
    }

    @Test
    void refreshRotatesOnceBeforeAccessExpiryWhileBusinessKeyWithNoExpiryRemainsUsable() throws Exception {
        Flow flow = start(); var auth = finish(flow);
        String original = market.refreshes.entrySet().stream().filter(e -> e.getValue().get("sub").equals(auth.get("sub"))).findFirst().orElseThrow().getKey();
        CustomerTestTime.offset.set(271_000);
        long deadline = System.nanoTime() + java.time.Duration.ofSeconds(5).toNanos();
        while (!market.refreshAttempts.containsKey(original) && System.nanoTime() < deadline) Thread.sleep(50);
        assertTrue(market.refreshAttempts.containsKey(original), "Expected proactive refresh at the external market boundary");
        Thread.sleep(500);
        assertEquals(1, market.refreshAttempts.get(original).get());
        assertEquals("ACTIVE", status(flow).get("authorization_status"));
        assertEquals("IDENTITY_VERIFIED", status(flow).get("status"));
    }

    Map<?,?> status(Flow flow) {
        var response = http.exchange("/customer/binding-flows/" + flow.id(), HttpMethod.GET, new HttpEntity<>(flow.headers()), Map.class);
        assertEquals(200, response.getStatusCode().value());
        return (Map<?,?>) response.getBody().get("data");
    }
    Map<String,String> finish(Flow flow) {
        String code = market.approve(flow.url());
        var auth = Map.copyOf(market.codes.get(code));
        var response = http.exchange("/customer/oidc/callback?code=" + code + "&state=" + auth.get("state"), HttpMethod.GET, new HttpEntity<>(flow.headers()), String.class);
        assertEquals(200, response.getStatusCode().value());
        assertEquals("IDENTITY_VERIFIED", status(flow).get("status"));
        return auth;
    }
    int logout(String token) {
        var headers = new HttpHeaders(); headers.setContentType(org.springframework.http.MediaType.APPLICATION_FORM_URLENCODED);
        var form = new org.springframework.util.LinkedMultiValueMap<String,String>(); form.add("logout_token", token);
        return http.postForEntity("/customer/oidc/backchannel-logout", new HttpEntity<>(form, headers), String.class).getStatusCode().value();
    }
    @Test
    void signedBackchannelNeedsNoCookieInvalidatesOnlyMatchingIdentityAndPreservesGrant() {
        Flow first = start(), other = start();
        var auth = finish(first); finish(other);
        String token = market.logout(Map.of("sid", auth.get("sid")));
        assertEquals(204, logout(token));
        assertEquals("SESSION_ENDED", status(first).get("status"));
        assertEquals("ACTIVE", status(first).get("authorization_status"));
        assertEquals("IDENTITY_VERIFIED", status(other).get("status"));
        assertEquals(204, logout(token));
    }

    @Test
    void transactionExpiresAtFiveMinutesAndPollingCannotExtendThirtyMinuteIdle() {
        Flow flow = start();
        String code = market.approve(flow.url());
        String state = ControlledMarket.params(java.net.URI.create(flow.url()).getRawQuery()).get("state");
        CustomerTestTime.offset.set(301_000);
        assertEquals(410, http.exchange("/customer/oidc/callback?code=" + code + "&state=" + state,
                HttpMethod.GET, new HttpEntity<>(flow.headers()), String.class).getStatusCode().value());
        CustomerTestTime.offset.set(1_799_000);
        assertEquals(200, http.exchange("/customer/binding-flows/" + flow.id(), HttpMethod.GET, new HttpEntity<>(flow.headers()), String.class).getStatusCode().value());
        CustomerTestTime.offset.set(1_801_000);
        assertEquals(401, http.exchange("/customer/binding-flows/" + flow.id(), HttpMethod.GET, new HttpEntity<>(flow.headers()), String.class).getStatusCode().value());
    }

    record Flow(HttpHeaders headers, String id, String url) {}
    Flow start() {
        String handle = request(600_000);
        var page = http.getForEntity("/bind/" + handle, String.class);
        var headers = new HttpHeaders();
        headers.set("Cookie", page.getHeaders().getFirst("Set-Cookie").split(";", 2)[0]);
        headers.set("X-CSRF-Token", page.getBody().split("name=\"csrf\" value=\"")[1].split("\"")[0]);
        headers.setOrigin("https://bridge.hncig.cn:18443");
        var response = http.postForEntity("/customer/binding-flows", new HttpEntity<>(Map.of("request_handle", handle), headers), Map.class);
        assertEquals(201, response.getStatusCode().value());
        var data = (Map<?,?>) response.getBody().get("data");
        return new Flow(headers, data.get("flow_id").toString(), data.get("authorization_url").toString());
    }
    @Test
    void oidcSuccessConfirmsIdentityAndRemovesCodeFromAddress() {
        Flow flow = start();
        String code = market.approve(flow.url());
        String state = ControlledMarket.params(java.net.URI.create(flow.url()).getRawQuery()).get("state");
        var callback = http.exchange("/customer/oidc/callback?code=" + code + "&state=" + state,
                HttpMethod.GET, new HttpEntity<>(flow.headers()), String.class);
        // TestRestTemplate follows the clean redirect to the result page.
        assertEquals(200, callback.getStatusCode().value());
        assertTrue(callback.getBody().contains("身份已确认，等待手机确认"));
        assertTrue(!callback.getBody().contains("test-business-key") && !callback.getBody().contains(code));
        var status = http.exchange("/customer/binding-flows/" + flow.id(), HttpMethod.GET, new HttpEntity<>(flow.headers()), Map.class);
        var data = (Map<?, ?>) status.getBody().get("data");
        assertEquals("IDENTITY_VERIFIED", data.get("status"));
        assertEquals("ACTIVE", data.get("authorization_status"));
    }

    @Test
    void flowRequiresBrowserCsrfAndSeparateTabsKeepTheirOwnTarget() {
        String handle = request(240_000);
        var page = http.getForEntity("/bind/" + handle, String.class);
        var headers = new HttpHeaders();
        headers.set("Cookie", page.getHeaders().getFirst("Set-Cookie").split(";", 2)[0]);
        var rejected = http.postForEntity("/customer/binding-flows", new HttpEntity<>(Map.of("request_handle", handle), headers), String.class);
        assertEquals(403, rejected.getStatusCode().value());
        String csrf = page.getBody().split("name=\"csrf\" value=\"")[1].split("\"")[0];
        headers.set("X-CSRF-Token", csrf);
        headers.setOrigin("https://bridge.hncig.cn:18443");
        var flow = http.postForEntity("/customer/binding-flows", new HttpEntity<>(Map.of("request_handle", handle), headers), Map.class);
        assertEquals(201, flow.getStatusCode().value());
        Map<?, ?> data = (Map<?, ?>) flow.getBody().get("data");
        String url = data.get("authorization_url").toString();
        assertTrue(url.startsWith(market.issuer + "/auth?"));
        assertTrue(url.contains("code_challenge_method=S256"));
        assertTrue(url.contains("nonce=") && url.contains("state=") && url.contains("prompt=consent"));
        assertTrue(!url.contains("test-only-secret"));
        var secondPage = http.exchange("/bind/" + request(200_000), HttpMethod.GET, new HttpEntity<>(headers), String.class);
        assertEquals(null, secondPage.getHeaders().getFirst("Set-Cookie"));
        var status = http.exchange("/customer/binding-flows/" + data.get("flow_id"), HttpMethod.GET, new HttpEntity<>(headers), Map.class);
        assertEquals("AWAITING_IDENTITY", ((Map<?, ?>) status.getBody().get("data")).get("status"));
    }

    private String request(long remainingMs) {
        String device = UUID.randomUUID().toString().replace("-", "");
        String handle = UUID.randomUUID().toString();
        db.update("INSERT INTO ai_device(id,alias) VALUES (?,?)", device, "Test speaker");
        db.update("INSERT INTO customer_device_request(handle,device_id,purpose,permission_version,expires_at_ms) VALUES (?,?,'bind',1,?)",
                handle, device, System.currentTimeMillis() + remainingMs);
        return handle;
    }

    @Test
    void validRequestOpensPhonePageAndExpiredRequestCannotBeUsed() {
        var page = http.getForEntity("/bind/" + request(240_000), String.class);
        assertEquals(200, page.getStatusCode().value());
        assertTrue(page.getBody().contains("Test speaker"));
        assertTrue(page.getBody().contains("csrf"));
        String cookie = page.getHeaders().getFirst("Set-Cookie");
        assertTrue(cookie.contains("HttpOnly") && cookie.contains("Secure") && cookie.contains("SameSite=Lax"));
        assertEquals("no-store", page.getHeaders().getFirst("Cache-Control"));
        assertEquals(410, http.getForEntity("/bind/" + request(-1000), String.class).getStatusCode().value());
    }

    @Test
    void unknownDeviceRequestDoesNotOpenAnIdentityFlow() {
        var response = http.getForEntity("/bind/unknown-request", String.class);
        assertEquals(404, response.getStatusCode().value());
    }
}
