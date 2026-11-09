package xiaozhi.modules.customer;

import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.net.URI;
import java.net.URLDecoder;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.nimbusds.jose.*;
import com.nimbusds.jose.crypto.RSASSASigner;
import com.nimbusds.jose.jwk.*;
import com.nimbusds.jose.jwk.gen.RSAKeyGenerator;
import com.nimbusds.jwt.*;
import com.sun.net.httpserver.HttpExchange;

/** The only substituted system is the external AI market. */
final class ControlledMarket {
    final HttpServer server;
    final String issuer;
    final Path config;
    final RSAKey rsa;
    final Map<String, Map<String,String>> codes = new ConcurrentHashMap<>();
    final Map<String, Map<String,String>> accesses = new ConcurrentHashMap<>();
    final Map<String, Map<String,String>> refreshes = new ConcurrentHashMap<>();
    final Map<String, java.util.concurrent.atomic.AtomicInteger> refreshAttempts = new ConcurrentHashMap<>();
    final Map<String,String> refreshModes = new ConcurrentHashMap<>();
    final Map<String,String> approvals = new ConcurrentHashMap<>();
    final Map<String,java.util.concurrent.CountDownLatch[]> refreshGates = new ConcurrentHashMap<>();
    final ObjectMapper json = new ObjectMapper();
    ControlledMarket() {
        try {
            rsa = new RSAKeyGenerator(2048).keyID("market-test").generate();
            server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
            server.setExecutor(java.util.concurrent.Executors.newCachedThreadPool(r -> { Thread t=new Thread(r); t.setDaemon(true); return t; }));
            issuer = "http://127.0.0.1:" + server.getAddress().getPort();
            server.createContext("/auth", x -> {
                String ticket=UUID.randomUUID().toString(); approvals.put(ticket,issuer+x.getRequestURI());
                byte[] body=("<!doctype html><html><meta charset=\"UTF-8\"><title>可控集市测试授权</title><h1>可控集市测试授权</h1>"
                        + "<form action=\"/approve\"><input type=\"hidden\" name=\"ticket\" value=\""+ticket+"\">"
                        + "<button name=\"decision\" value=\"allow\">允许</button><button name=\"decision\" value=\"deny\">拒绝</button></form></html>").getBytes(StandardCharsets.UTF_8);
                x.getResponseHeaders().set("Content-Type","text/html; charset=utf-8");x.sendResponseHeaders(200,body.length);x.getResponseBody().write(body);x.close();
            });
            server.createContext("/approve", x -> {
                var form=params(x.getRequestURI().getRawQuery());String url=approvals.remove(form.get("ticket"));
                if (url==null) {reply(x,400,Map.of("error","invalid_ticket"));return;}
                var auth=params(URI.create(url).getRawQuery());
                String query="allow".equals(form.get("decision")) ? "code="+approve(url) : "error=access_denied";
                x.getResponseHeaders().set("Location",auth.get("redirect_uri")+"?"+query+"&state="+auth.get("state"));
                x.sendResponseHeaders(303,-1);x.close();
            });
            server.createContext("/.well-known/openid-configuration", exchange -> {
                byte[] body = ("{\"issuer\":\"" + issuer + "\",\"authorization_endpoint\":\"" + issuer
                        + "/auth\",\"token_endpoint\":\"" + issuer + "/token\",\"userinfo_endpoint\":\""
                        + issuer + "/me\",\"jwks_uri\":\"" + issuer + "/jwks\","
                        + "\"response_types_supported\":[\"code\"],\"subject_types_supported\":[\"public\"],"
                        + "\"id_token_signing_alg_values_supported\":[\"RS256\"]}").getBytes(StandardCharsets.UTF_8);
                exchange.getResponseHeaders().set("Content-Type", "application/json");
                exchange.sendResponseHeaders(200, body.length);
                exchange.getResponseBody().write(body);
                exchange.close();
            });
            server.createContext("/jwks", x -> reply(x, 200, new JWKSet(rsa.toPublicJWK()).toJSONObject()));
            server.createContext("/token", x -> {
                try {
                    String expectedBasic = "Basic " + Base64.getEncoder().encodeToString("test-customer:test-only-secret".getBytes(StandardCharsets.UTF_8));
                    if (!expectedBasic.equals(x.getRequestHeaders().getFirst("Authorization"))) { reply(x, 401, Map.of("error", "invalid_client")); return; }
                    var form = params(new String(x.getRequestBody().readAllBytes(), StandardCharsets.UTF_8));
                    if ("refresh_token".equals(form.get("grant_type"))) {
                        String oldToken = form.get("refresh_token");
                        refreshAttempts.computeIfAbsent(oldToken, k -> new java.util.concurrent.atomic.AtomicInteger()).incrementAndGet();
                        var auth = refreshes.remove(oldToken);
                        if (auth == null) { reply(x, 400, Map.of("error", "invalid_grant")); return; }
                        String mode = refreshModes.getOrDefault(auth.get("sub"), "ok");
                        var gate=refreshGates.get(oldToken);
                        if (gate!=null) {gate[0].countDown();gate[1].await(5,java.util.concurrent.TimeUnit.SECONDS);}
                        if (mode.equals("invalid")) { reply(x, 400, Map.of("error", "invalid_grant")); return; }
                        if (mode.equals("lost")) { x.close(); return; }
                        if (mode.equals("server")) { reply(x, 503, Map.of("error", "temporarily_unavailable")); return; }
                        String next = UUID.randomUUID().toString();
                        accesses.put(next, auth); refreshes.put("refresh-" + next, auth);
                        reply(x, 200, Map.of("access_token", next, "refresh_token", "refresh-" + next, "token_type", "Bearer", "expires_in", 300));
                        return;
                    }
                    var auth = codes.remove(form.get("code"));
                    if (auth == null) { reply(x, 400, Map.of("error", "invalid_grant")); return; }
                    String fault = auth.getOrDefault("fault", "none");
                    if (fault.equals("network")) { reply(x, 503, Map.of("error", "temporarily_unavailable")); return; }
                    String challenge = Base64.getUrlEncoder().withoutPadding().encodeToString(java.security.MessageDigest.getInstance("SHA-256")
                            .digest(form.get("code_verifier").getBytes(StandardCharsets.US_ASCII)));
                    if (!challenge.equals(auth.get("code_challenge")) || !"https://bridge.hncig.cn:18443/xiaozhi/customer/oidc/callback".equals(form.get("redirect_uri"))) {
                        reply(x, 400, Map.of("error", "invalid_grant")); return;
                    }
                    String access = UUID.randomUUID().toString();
                    accesses.put(access, auth);
                    refreshes.put("refresh-" + access, auth);
                    var claims = new JWTClaimsSet.Builder().issuer(fault.equals("issuer") ? "https://wrong.example" : issuer)
                            .subject(auth.get("sub")).audience(fault.equals("audience") ? "other-client" : "test-customer")
                            .issueTime(new Date()).expirationTime(new Date(System.currentTimeMillis() + (fault.equals("expired") ? -120_000 : 300_000)))
                            .claim("nonce", fault.equals("nonce") ? "wrong-nonce" : auth.get("nonce")).claim("sid", auth.get("sid")).build();
                    var jwt = new SignedJWT(new JWSHeader.Builder(JWSAlgorithm.RS256).keyID(rsa.getKeyID()).build(), claims);
                    jwt.sign(new RSASSASigner(fault.equals("signature") ? new RSAKeyGenerator(2048).generate() : rsa));
                    reply(x, 200, Map.of("access_token", access, "refresh_token", "refresh-" + access, "token_type", "Bearer",
                            "expires_in", 300, "scope", "openid profile credentials:manage offline_access", "id_token", jwt.serialize()));
                } catch (Exception e) { reply(x, 500, Map.of("error", "test_provider_error")); }
            });
            server.createContext("/me", x -> {
                var auth = authorization(x);
                reply(x, auth == null ? 401 : 200, auth == null ? Map.of("error", "invalid_token") : Map.of("sub", "userinfo".equals(auth.get("fault")) ? "another-user" : auth.get("sub")));
            });
            server.createContext("/api/application-credentials", x -> {
                var auth = authorization(x);
                var data = new HashMap<String,Object>();
                if (auth != null) {
                    data.put("subject", auth.get("sub")); data.put("client_id", "test-customer");
                    data.put("api_key", "test-business-key"); data.put("expires_at", null); data.put("base_url", issuer);
                }
                reply(x, auth == null ? 401 : 201, Map.of("code", auth == null ? 401 : 20000, "data", data));
            });
            server.start();
            config = Files.createTempFile("customer-oidc-test-", ".json");
            Files.setPosixFilePermissions(config, PosixFilePermissions.fromString("rw-------"));
            Files.writeString(config, json.writeValueAsString(Map.of("issuer", issuer, "client_id", "test-customer", "client_secret", "test-only-secret",
                    "redirect_uri", "https://bridge.hncig.cn:18443/xiaozhi/customer/oidc/callback",
                    "backchannel_logout_uri", "https://bridge.hncig.cn:18443/xiaozhi/customer/oidc/backchannel-logout")));
            Path keyFile = Path.of(config + ".key");
            Files.writeString(keyFile, java.util.Base64.getEncoder().encodeToString(new byte[32]));
            Files.setPosixFilePermissions(keyFile, PosixFilePermissions.fromString("rw-------"));
            keyFile.toFile().deleteOnExit();
            config.toFile().deleteOnExit();
        } catch (Exception e) { throw new IllegalStateException(e); }
    }
    Map<String,String> authorization(HttpExchange x) {
        String value = x.getRequestHeaders().getFirst("Authorization");
        return value != null && value.startsWith("Bearer ") ? accesses.get(value.substring(7)) : null;
    }
    void reply(HttpExchange x, int status, Object value) throws java.io.IOException {
        byte[] body = json.writeValueAsBytes(value);
        x.getResponseHeaders().set("Content-Type", "application/json");
        x.sendResponseHeaders(status, body.length); x.getResponseBody().write(body); x.close();
    }
    static Map<String,String> params(String query) {
        Map<String,String> result = new HashMap<>();
        for (String pair : query.split("&")) {
            String[] parts = pair.split("=", 2);
            result.put(URLDecoder.decode(parts[0], StandardCharsets.UTF_8), URLDecoder.decode(parts.length > 1 ? parts[1] : "", StandardCharsets.UTF_8));
        }
        return result;
    }
    String approve(String authorizationUrl) {
        var auth = params(URI.create(authorizationUrl).getRawQuery());
        auth.put("sub", UUID.randomUUID().toString()); auth.put("sid", UUID.randomUUID().toString());
        String code = UUID.randomUUID().toString(); codes.put(code, auth); return code;
    }
    String logout(Map<String,Object> overrides) {
        try {
            Map<String,Object> claims = new HashMap<>();
            claims.put("iss", issuer); claims.put("aud", "test-customer");
            claims.put("iat", System.currentTimeMillis() / 1000); claims.put("exp", System.currentTimeMillis() / 1000 + 300);
            claims.put("jti", UUID.randomUUID().toString());
            claims.put("events", Map.of("http://schemas.openid.net/event/backchannel-logout", Map.of()));
            claims.putAll(overrides);
            var jwt = new SignedJWT(new JWSHeader.Builder(JWSAlgorithm.RS256).keyID(rsa.getKeyID()).build(), JWTClaimsSet.parse(claims));
            jwt.sign(new RSASSASigner(rsa)); return jwt.serialize();
        } catch (Exception e) { throw new IllegalStateException(e); }
    }
}
