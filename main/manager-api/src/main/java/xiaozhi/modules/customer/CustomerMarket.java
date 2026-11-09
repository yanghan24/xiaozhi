package xiaozhi.modules.customer;

import com.nimbusds.oauth2.sdk.ResponseType;
import com.nimbusds.oauth2.sdk.Scope;
import com.nimbusds.oauth2.sdk.id.ClientID;
import com.nimbusds.oauth2.sdk.id.State;
import com.nimbusds.oauth2.sdk.pkce.CodeVerifier;
import com.nimbusds.oauth2.sdk.pkce.CodeChallenge;
import com.nimbusds.oauth2.sdk.pkce.CodeChallengeMethod;
import com.nimbusds.openid.connect.sdk.AuthenticationRequest;
import com.nimbusds.openid.connect.sdk.Nonce;
import com.nimbusds.openid.connect.sdk.Prompt;
import com.nimbusds.openid.connect.sdk.op.OIDCProviderMetadata;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import org.springframework.stereotype.Component;
import com.nimbusds.oauth2.sdk.*;
import com.nimbusds.oauth2.sdk.auth.*;
import com.nimbusds.oauth2.sdk.id.Issuer;
import com.nimbusds.openid.connect.sdk.*;
import com.nimbusds.openid.connect.sdk.validators.IDTokenValidator;
import com.nimbusds.jose.JWSAlgorithm;
import com.nimbusds.jose.jwk.JWKSet;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.Map;

@Component
final class CustomerMarket {
    private final CustomerOidcConfig config;
    private final HttpClient http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5))
            .followRedirects(HttpClient.Redirect.NEVER).build();
    private volatile OIDCProviderMetadata metadata;
    private final java.time.Clock clock;
    CustomerMarket(CustomerOidcConfig config, java.time.Clock clock) { this.config = config; this.clock = clock; }
    record Identity(String subject, String sid, long issuedAt, String access, String refresh,
                    long accessExpiresAt, String businessKey, Long keyExpiresAt) {
        @Override public String toString() { return "Identity[redacted]"; }
    }
    record Logout(String issuer, String jti, String sid, String subject, long issuedAt, long expiresAt) {}
    record Refreshed(String access, String refresh, long expiresAt) {
        @Override public String toString() { return "Refreshed[redacted]"; }
    }
    Refreshed refresh(String refreshToken) {
        try {
            var request = new TokenRequest(metadata().getTokenEndpointURI(),
                    new ClientSecretBasic(new ClientID(config.clientId), new Secret(config.clientSecret)),
                    new RefreshTokenGrant(new com.nimbusds.oauth2.sdk.token.RefreshToken(refreshToken))).toHTTPRequest();
            var response = http.send(HttpRequest.newBuilder(request.getURI()).timeout(Duration.ofSeconds(8))
                    .header("Authorization", request.getAuthorization()).header("Content-Type", "application/x-www-form-urlencoded")
                    .POST(HttpRequest.BodyPublishers.ofString(request.getQuery())).build(), HttpResponse.BodyHandlers.ofString());
            var incoming = new com.nimbusds.oauth2.sdk.http.HTTPResponse(response.statusCode());
            incoming.setContentType("application/json"); incoming.setContent(response.body());
            var parsed = TokenResponse.parse(incoming);
            if (!parsed.indicatesSuccess()) {
                String error = parsed.toErrorResponse().getErrorObject().getCode();
                if (response.statusCode() == 400 && "invalid_grant".equals(error)) throw new CustomerFailure(401, "REAUTH_REQUIRED");
                throw new CustomerFailure(502, "REFRESH_UNCERTAIN");
            }
            var tokens = parsed.toSuccessResponse().getTokens();
            if (tokens.getRefreshToken() == null || tokens.getRefreshToken().getValue().equals(refreshToken)
                    || tokens.getAccessToken().getLifetime() <= 0 || tokens.getAccessToken().getLifetime() > 300)
                throw new CustomerFailure(502, "REFRESH_UNCERTAIN");
            return new Refreshed(tokens.getAccessToken().getValue(), tokens.getRefreshToken().getValue(), clock.millis() + tokens.getAccessToken().getLifetime() * 1000);
        } catch (CustomerFailure e) { throw e; }
        catch (Exception e) { throw new CustomerFailure(502, "REFRESH_UNCERTAIN"); }
    }
    Logout validateLogout(String token) {
        if (token == null || token.length() > 16384) throw new CustomerFailure(400, "INVALID_LOGOUT_TOKEN");
        try {
            var jwt = com.nimbusds.jwt.SignedJWT.parse(token);
            new com.nimbusds.openid.connect.sdk.validators.LogoutTokenValidator(new Issuer(config.issuer),
                    new ClientID(config.clientId), JWSAlgorithm.RS256, jwks()).validate(jwt);
            var claims = jwt.getJWTClaimsSet();
            long now = System.currentTimeMillis();
            String sid = claims.getStringClaim("sid"), sub = claims.getSubject(), jti = claims.getJWTID();
            var events = claims.getJSONObjectClaim("events");
            if (claims.getIssueTime() == null || claims.getExpirationTime() == null
                    || claims.getIssueTime().getTime() > now + 30_000 || claims.getIssueTime().getTime() < now - 300_000
                    || claims.getExpirationTime().getTime() <= now || claims.getExpirationTime().before(claims.getIssueTime())
                    || claims.getClaims().containsKey("nonce") || (sid == null && sub == null)
                    || jti == null || jti.isBlank() || jti.length() > 255
                    || (sid != null && (sid.isBlank() || sid.length() > 255)) || (sub != null && (sub.isBlank() || sub.length() > 255))
                    || events == null || !(events.get("http://schemas.openid.net/event/backchannel-logout") instanceof Map))
                throw new IllegalArgumentException();
            return new Logout(config.issuer, jti, sid, sub, claims.getIssueTime().getTime(), claims.getExpirationTime().getTime());
        } catch (Exception e) { throw new CustomerFailure(400, "INVALID_LOGOUT_TOKEN"); }
    }

    Identity exchange(String code, String verifier, String nonce) {
        try {
            var tokenRequest = new TokenRequest(metadata().getTokenEndpointURI(),
                    new ClientSecretBasic(new ClientID(config.clientId), new Secret(config.clientSecret)),
                    new AuthorizationCodeGrant(new AuthorizationCode(code), CustomerOidcConfig.CALLBACK, new CodeVerifier(verifier)));
            var outgoing = tokenRequest.toHTTPRequest();
            var response = http.send(HttpRequest.newBuilder(outgoing.getURI()).timeout(Duration.ofSeconds(8))
                    .header("Authorization", outgoing.getAuthorization())
                    .header("Content-Type", "application/x-www-form-urlencoded")
                    .POST(HttpRequest.BodyPublishers.ofString(outgoing.getQuery())).build(), HttpResponse.BodyHandlers.ofString());
            var incoming = new com.nimbusds.oauth2.sdk.http.HTTPResponse(response.statusCode());
            incoming.setContentType("application/json"); incoming.setContent(response.body());
            var parsed = OIDCTokenResponseParser.parse(incoming);
            if (!parsed.indicatesSuccess()) throw new CustomerFailure(502, "IDENTITY_EXCHANGE_FAILED");
            var tokens = ((OIDCTokenResponse) parsed).getOIDCTokens();
            if (tokens.getIDToken() == null || tokens.getRefreshToken() == null) throw new CustomerFailure(502, "AUTHORIZATION_INCOMPLETE");
            var validator = new IDTokenValidator(new Issuer(config.issuer), new ClientID(config.clientId), JWSAlgorithm.RS256, jwks());
            validator.setMaxClockSkew(30);
            var claims = validator.validate(tokens.getIDToken(), new Nonce(nonce));
            String sub = claims.getSubject().getValue();
            String access = tokens.getAccessToken().getValue();
            Map<?,?> user = jsonRequest(metadata().getUserInfoEndpointURI(), "GET", access);
            if (!sub.equals(user.get("sub"))) throw new CustomerFailure(502, "IDENTITY_MISMATCH");
            if (tokens.getAccessToken().getScope() != null && !tokens.getAccessToken().getScope().contains("credentials:manage"))
                throw new CustomerFailure(502, "AUTHORIZATION_INCOMPLETE");
            URI issuer = URI.create(config.issuer);
            URI credentials = new URI(issuer.getScheme(), null, issuer.getHost(), issuer.getPort(), "/api/application-credentials", null, null);
            Map<?,?> envelope = jsonRequest(credentials, "POST", access);
            if (!Integer.valueOf(20000).equals(envelope.get("code")) || !(envelope.get("data") instanceof Map<?,?> data)
                    || !sub.equals(data.get("subject")) || !config.clientId.equals(data.get("client_id"))
                    || !(data.get("api_key") instanceof String key) || key.isBlank()) throw new CustomerFailure(502, "AUTHORIZATION_INCOMPLETE");
            Object expiry = data.get("expires_at");
            Long keyExpires = expiry == null ? null : java.time.Instant.parse(expiry.toString()).toEpochMilli();
            long lifetime = tokens.getAccessToken().getLifetime();
            if (lifetime <= 0 || lifetime > 300) throw new CustomerFailure(502, "AUTHORIZATION_INCOMPLETE");
            return new Identity(sub, claims.getStringClaim("sid"), claims.getIssueTime().getTime(), access,
                    tokens.getRefreshToken().getValue(), clock.millis() + lifetime * 1000, key, keyExpires);
        } catch (CustomerFailure e) { throw e; }
        catch (Exception e) { throw new CustomerFailure(502, "IDENTITY_VERIFICATION_FAILED"); }
    }
    JWKSet jwks() throws Exception {
        var response = http.send(HttpRequest.newBuilder(metadata().getJWKSetURI()).timeout(Duration.ofSeconds(5)).GET().build(), HttpResponse.BodyHandlers.ofString());
        if (response.statusCode() != 200) throw new IllegalArgumentException();
        return JWKSet.parse(response.body());
    }
    Map<?,?> jsonRequest(URI uri, String method, String access) throws Exception {
        var response = http.send(HttpRequest.newBuilder(uri).timeout(Duration.ofSeconds(8)).header("Authorization", "Bearer " + access)
                .method(method, HttpRequest.BodyPublishers.noBody()).build(), HttpResponse.BodyHandlers.ofString());
        if (response.statusCode() < 200 || response.statusCode() >= 300) throw new CustomerFailure(502, "MARKET_REQUEST_FAILED");
        return new ObjectMapper().readValue(response.body(), Map.class);
    }
    OIDCProviderMetadata metadata() {
        config.requireEnabled();
        if (metadata != null) return metadata;
        try {
            var response = http.send(HttpRequest.newBuilder(URI.create(config.issuer + "/.well-known/openid-configuration"))
                    .timeout(Duration.ofSeconds(5)).GET().build(), HttpResponse.BodyHandlers.ofString());
            if (response.statusCode() != 200) throw new IllegalArgumentException();
            var parsed = OIDCProviderMetadata.parse(response.body());
            if (!parsed.getIssuer().getValue().equals(config.issuer)) throw new IllegalArgumentException();
            URI base = URI.create(config.issuer);
            for (URI endpoint : new URI[]{parsed.getAuthorizationEndpointURI(), parsed.getTokenEndpointURI(),
                    parsed.getUserInfoEndpointURI(), parsed.getJWKSetURI()}) {
                if (endpoint == null || !base.getScheme().equals(endpoint.getScheme()) || !base.getHost().equals(endpoint.getHost())
                        || base.getPort() != endpoint.getPort() || endpoint.getUserInfo() != null || endpoint.getFragment() != null)
                    throw new IllegalArgumentException();
            }
            metadata = parsed;
            return parsed;
        } catch (Exception e) { throw new CustomerFailure(502, "MARKET_UNAVAILABLE"); }
    }
    String authorizationUrl(String state, String nonce, String verifier) {
        return new AuthenticationRequest.Builder(ResponseType.CODE,
                Scope.parse("openid profile credentials:manage offline_access"), new ClientID(config.clientId), CustomerOidcConfig.CALLBACK)
                .endpointURI(metadata().getAuthorizationEndpointURI()).state(new State(state)).nonce(new Nonce(nonce))
                .codeChallenge(CodeChallenge.compute(CodeChallengeMethod.S256, new CodeVerifier(verifier)), CodeChallengeMethod.S256)
                .prompt(new Prompt(Prompt.Type.CONSENT)).build().toURI().toString();
    }
}
