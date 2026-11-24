package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.Date;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import com.sun.net.httpserver.HttpServer;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.nimbusds.jose.JOSEObjectType;
import com.nimbusds.jose.JWSAlgorithm;
import com.nimbusds.jose.JWSHeader;
import com.nimbusds.jose.crypto.RSASSASigner;
import com.nimbusds.jose.jwk.JWKSet;
import com.nimbusds.jose.jwk.RSAKey;
import com.nimbusds.jose.jwk.gen.RSAKeyGenerator;
import com.nimbusds.jwt.JWTClaimsSet;
import com.nimbusds.jwt.SignedJWT;
import org.junit.jupiter.api.*;

/** Only a loopback protocol fixture. No production authentication, models or billing. */
class SpeakerMarketTest {
    private HttpServer server;
    private String issuer;
    private SpeakerMarket market;
    private RSAKey rsa;
    private final ObjectMapper json=new ObjectMapper();
    private final Map<String,String> bodies=new ConcurrentHashMap<>();
    private final Map<String,Integer> statuses=new ConcurrentHashMap<>();
    private final java.util.concurrent.atomic.AtomicInteger tokenRequests = new java.util.concurrent.atomic.AtomicInteger();
    private volatile String retryAfter;
    private volatile boolean dropTokenResponse;
    private String lastForm;
    @BeforeEach void start() throws Exception {
        server=HttpServer.create(new InetSocketAddress("127.0.0.1",0),0);
        issuer="http://127.0.0.1:"+server.getAddress().getPort()+"/oidc";
        rsa=new RSAKeyGenerator(2048).keyID("test-key").generate();
        bodies.put("/oidc/.well-known/openid-configuration",json.writeValueAsString(Map.of(
                "issuer",issuer,"device_authorization_endpoint",issuer+"/device/auth","token_endpoint",issuer+"/token",
                "userinfo_endpoint",issuer+"/me","jwks_uri",issuer+"/jwks")));
        bodies.put("/oidc/jwks",new JWKSet(rsa.toPublicJWK()).toString());
        server.createContext("/",exchange -> {
            lastForm=new String(exchange.getRequestBody().readAllBytes(),StandardCharsets.UTF_8);
            // Public device clients must never send a Client Secret or HTTP Basic.
            if(exchange.getRequestURI().getPath().equals("/oidc/token")) {
                tokenRequests.incrementAndGet();
                assertNull(exchange.getRequestHeaders().getFirst("Authorization"));
                if (dropTokenResponse) {
                    exchange.close();
                    return;
                }
            }
            if (retryAfter != null) exchange.getResponseHeaders().add("Retry-After",retryAfter);
            byte[] body=bodies.getOrDefault(exchange.getRequestURI().getPath(),"{}").getBytes(StandardCharsets.UTF_8);
            exchange.getResponseHeaders().add("Content-Type","application/json");
            exchange.sendResponseHeaders(statuses.getOrDefault(exchange.getRequestURI().getPath(),200),body.length);
            try(var output=exchange.getResponseBody()) { output.write(body); }
        });
        server.start();
        market=new SpeakerMarket(new SpeakerSettings(true,issuer,"test-client","",""));
    }
    @AfterEach void stop() { server.stop(0); }
    private String signed(String subject,String audience) throws Exception {
        var claims=new JWTClaimsSet.Builder().issuer(issuer).subject(subject).audience(audience)
                .issueTime(new Date()).expirationTime(new Date(System.currentTimeMillis()+300000)).build();
        var jwt=new SignedJWT(new JWSHeader.Builder(JWSAlgorithm.RS256).keyID("test-key").type(JOSEObjectType.JWT).build(),claims);
        jwt.sign(new RSASSASigner(rsa)); return jwt.serialize();
    }
    private void tokens(String audience) throws Exception {
        bodies.put("/oidc/token",json.writeValueAsString(Map.of("access_token","access-fixture","refresh_token","refresh-fixture",
                "expires_in",300,"token_type","Bearer","id_token",signed("owner",audience),"scope",SpeakerMarket.SCOPES)));
    }
    @Test void usesDeviceGrantWithoutSecretOrCallback() throws Exception {
        bodies.put("/oidc/device/auth",json.writeValueAsString(Map.of("device_code","secret-device-fixture","user_code","ABCD-EFGH",
                "verification_uri_complete",issuer+"/device?user_code=ABCD-EFGH","expires_in",600,"interval",5)));
        var request=market.begin();
        assertEquals("ABCD-EFGH",request.userCode());
        assertFalse(request.toString().contains("secret-device-fixture"));
        assertTrue(lastForm.contains("client_id=test-client"));
        assertFalse(lastForm.contains("client_secret")); assertFalse(lastForm.contains("redirect_uri"));
    }
    @Test void distinguishesPendingAndSlowDownFromDenial() {
        statuses.put("/oidc/token",400);
        for(String error:new String[]{"authorization_pending","slow_down","access_denied","expired_token","invalid_client"}) {
            bodies.put("/oidc/token","{\"error\":\""+error+"\"}");
            assertEquals(error,market.poll("device-fixture").error());
        }
    }
    @Test void verifiesDeviceIdentityThenChecksUserInfoSubject() throws Exception {
        tokens("test-client"); var token=market.poll("device-fixture").tokens();
        assertEquals("owner",token.subject());
        assertFalse(token.toString().contains("access-fixture"));
        bodies.put("/oidc/me","{\"sub\":\"other-user\"}");
        assertEquals("MARKET_IDENTITY_MISMATCH",assertThrows(SpeakerFailure.class,()->market.profile(token)).getMessage());
    }
    @Test void rejectsIdentityIssuedToAnotherApplication() throws Exception {
        tokens("another-client");
        assertEquals("INVALID_MARKET_IDENTITY",assertThrows(SpeakerFailure.class,()->market.poll("device-fixture")).getMessage());
    }
    @Test void rejectsDiscoveryRedirectingTokensToAnotherAuthority() throws Exception {
        bodies.put("/oidc/.well-known/openid-configuration",json.writeValueAsString(Map.of("issuer",issuer,
                "device_authorization_endpoint","https://other.example/device","token_endpoint",issuer+"/token",
                "userinfo_endpoint",issuer+"/me","jwks_uri",issuer+"/jwks")));
        assertThrows(SpeakerFailure.class,()->market.begin());
    }
    @Test void keyProvisionFailureDoesNotMutateSuccessfulTokens() throws Exception {
        tokens("test-client"); var token=market.poll("device-fixture").tokens();
        statuses.put("/api/application-credentials",503);
        bodies.put("/api/application-credentials","{\"code\":503}");
        assertEquals(503,assertThrows(SpeakerFailure.class,()->market.credential(token)).status);
        assertEquals("refresh-fixture",token.refresh());
        statuses.put("/api/application-credentials",201);
        bodies.put("/api/application-credentials",json.writeValueAsString(Map.of("code",20000,"data",Map.of(
                "subject","owner","client_id","test-client","api_key","key-fixture","base_url",issuer.replace("/oidc","")))));
        assertEquals("key-fixture",market.credential(token).key());
    }
    @Test void refreshRejectionIsDifferentFromTemporaryFailure() {
        statuses.put("/oidc/token",400);bodies.put("/oidc/token","{\"error\":\"invalid_grant\"}");
        assertEquals("MARKET_REAUTH_REQUIRED",assertThrows(SpeakerFailure.class,()->market.refresh("refresh-fixture","owner")).getMessage());
        statuses.put("/oidc/token",503);bodies.put("/oidc/token","{}");
        assertEquals("MARKET_UNAVAILABLE",assertThrows(SpeakerFailure.class,()->market.refresh("refresh-fixture","owner")).getMessage());
    }

    @Test void outageInDiscoveryDoesNotSendTheRefreshToken() {
        String discovery = "/oidc/.well-known/openid-configuration";
        statuses.put(discovery,502);
        var retry = assertThrows(SpeakerMarket.RefreshRetry.class,()->market.refresh("refresh-fixture","owner"));
        assertEquals("MARKET_REFRESH_NOT_SENT",retry.getMessage());
        assertEquals(0,tokenRequests.get());
    }

    @Test void gatewayErrorAfterSubmittingTokenDoesNotAuthorizeRetry() {
        statuses.put("/oidc/token",502);
        bodies.put("/oidc/token","<html>Bad gateway</html>");
        assertThrows(SpeakerFailure.class,()->market.refresh("refresh-fixture","owner"));
        assertEquals(1,tokenRequests.get());
    }

    @Test void lostTokenResponseIsNeverResentByTheHttpClient() {
        dropTokenResponse = true;
        assertThrows(SpeakerFailure.class,()->market.refresh("refresh-fixture","owner"));
        assertEquals(1,tokenRequests.get());
    }

    @Test void refreshRateLimitHonorsRetryAfterEvenWithoutAnOAuthResponse() {
        statuses.put("/oidc/token",429);
        bodies.put("/oidc/token","Busy");
        retryAfter="17";
        var retry = assertThrows(SpeakerMarket.RefreshRetry.class,()->market.refresh("refresh-fixture","owner"));
        assertEquals("MARKET_RATE_LIMITED",retry.getMessage());
        assertEquals(17_000,retry.retryAfterMs);
    }

    @Test void refreshClientAndScopeErrorsAreConfigurationFailures() {
        statuses.put("/oidc/token",401);
        bodies.put("/oidc/token","{\"error\":\"invalid_client\"}");
        assertEquals("MARKET_CLIENT_CONFIGURATION",
                assertThrows(SpeakerFailure.class,()->market.refresh("refresh-fixture","owner")).getMessage());
        statuses.put("/oidc/token",400);
        bodies.put("/oidc/token","{\"error\":\"invalid_scope\"}");
        assertEquals("MARKET_REFRESH_CONFIGURATION",
                assertThrows(SpeakerFailure.class,()->market.refresh("refresh-fixture","owner")).getMessage());
    }
}
