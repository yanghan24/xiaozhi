package xiaozhi.modules.speaker;

import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.nimbusds.jose.JWSAlgorithm;
import com.nimbusds.jose.jwk.JWKSet;
import com.nimbusds.jwt.SignedJWT;
import com.nimbusds.oauth2.sdk.id.ClientID;
import com.nimbusds.oauth2.sdk.id.Issuer;
import com.nimbusds.openid.connect.sdk.validators.IDTokenValidator;
import org.springframework.stereotype.Component;

/** AI Market's public device application contract. No website login, callback or client secret. */
@Component
public class SpeakerMarket {
    public static final String SCOPES = "openid profile organization balance:read credentials:manage offline_access";
    private final SpeakerSettings settings;
    private final ObjectMapper json = new ObjectMapper();
    private final HttpClient http = HttpClient.newBuilder().followRedirects(HttpClient.Redirect.NEVER)
            .connectTimeout(Duration.ofSeconds(5)).build();
    private volatile JsonNode discovery;
    public SpeakerMarket(SpeakerSettings settings) { this.settings = settings; }

    public record DeviceAuthorization(String deviceCode,String userCode,String verificationUri,long expiresIn,long interval) {
        @Override public String toString() { return "DeviceAuthorization[redacted]"; }
    }
    public record Tokens(String subject,String access,String refresh,long expiresAt,String scope) {
        @Override public String toString() { return "Tokens[redacted]"; }
    }
    public record Poll(Tokens tokens,String error) {
        @Override public String toString() { return "Poll[redacted]"; }
    }
    public record Credential(String key,String baseUrl,Long expiresAt) {
        @Override public String toString() { return "Credential[redacted]"; }
    }
    private URI issuer() {
        settings.requireEnabled();
        if (settings.clientId().isBlank()) throw new SpeakerFailure(503,"MARKET_CLIENT_NOT_CONFIGURED");
        URI uri;
        try { uri=URI.create(settings.issuer()); } catch (Exception e) { throw new SpeakerFailure(503,"INVALID_MARKET_CONFIG"); }
        boolean production = settings.issuer().equals("https://aimarket.hncig.cn/oidc");
        boolean fixture = "http".equals(uri.getScheme()) && "127.0.0.1".equals(uri.getHost()) && "/oidc".equals(uri.getPath());
        if ((!production && !fixture) || uri.getUserInfo()!=null || uri.getQuery()!=null || uri.getFragment()!=null)
            throw new SpeakerFailure(503,"INVALID_MARKET_CONFIG");
        return uri;
    }
    private URI trusted(String value) {
        try {
            URI endpoint=URI.create(value), base=issuer();
            if (!base.getScheme().equals(endpoint.getScheme()) || !base.getHost().equals(endpoint.getHost())
                    || base.getPort()!=endpoint.getPort() || endpoint.getUserInfo()!=null || endpoint.getFragment()!=null)
                throw new IllegalArgumentException();
            return endpoint;
        } catch (SpeakerFailure failure) { throw failure; }
        catch (Exception failure) { throw new SpeakerFailure(502,"INVALID_MARKET_ENDPOINT"); }
    }
    private JsonNode metadata() {
        if (discovery != null) return discovery;
        JsonNode node=request(URI.create(issuer()+"/.well-known/openid-configuration"),"GET",null,null,false);
        if (!settings.issuer().equals(node.path("issuer").asText())) throw new SpeakerFailure(502,"INVALID_MARKET_ISSUER");
        for(String field:new String[]{"device_authorization_endpoint","token_endpoint","userinfo_endpoint","jwks_uri"})
            trusted(node.path(field).asText());
        discovery=node;
        return node;
    }
    private URI endpoint(String name) { return trusted(metadata().path(name).asText()); }
    private static String form(Map<String,String> values) {
        return values.entrySet().stream().map(e -> URLEncoder.encode(e.getKey(),StandardCharsets.UTF_8)+"="
                +URLEncoder.encode(e.getValue(),StandardCharsets.UTF_8)).collect(Collectors.joining("&"));
    }
    private JsonNode request(URI uri,String method,String access,String body,boolean oauthError) {
        try {
            var request=HttpRequest.newBuilder(uri).timeout(Duration.ofSeconds(8)).header("Accept","application/json");
            if(access!=null) request.header("Authorization","Bearer "+access);
            if(body!=null) request.header("Content-Type","application/x-www-form-urlencoded");
            var response=http.send(request.method(method,body==null?HttpRequest.BodyPublishers.noBody():HttpRequest.BodyPublishers.ofString(body)).build(),HttpResponse.BodyHandlers.ofInputStream());
            byte[] raw;
            try(var stream=response.body()) { raw=stream.readNBytes(65537); }
            if(raw.length>65536) throw new SpeakerFailure(502,"INVALID_MARKET_RESPONSE");
            JsonNode node=json.readTree(raw);
            if(node==null || !node.isObject()) throw new SpeakerFailure(502,"INVALID_MARKET_RESPONSE");
            if(response.statusCode()>=200 && response.statusCode()<300) return node;
            if(oauthError && response.statusCode()==400 && Set.of("authorization_pending","slow_down","access_denied",
                    "expired_token","invalid_grant","invalid_client","invalid_scope").contains(node.path("error").asText())) return node;
            if(response.statusCode()==401) throw new SpeakerFailure(401,"MARKET_AUTH_REQUIRED");
            if(response.statusCode()==403) throw new SpeakerFailure(403,"MARKET_FORBIDDEN");
            if(response.statusCode()==429) throw new SpeakerFailure(429,"MARKET_RATE_LIMITED");
            throw new SpeakerFailure(503,"MARKET_UNAVAILABLE");
        } catch (SpeakerFailure failure) { throw failure; }
        catch (InterruptedException failure) { Thread.currentThread().interrupt(); throw new SpeakerFailure(503,"MARKET_UNAVAILABLE"); }
        catch (Exception failure) { throw new SpeakerFailure(503,"MARKET_UNAVAILABLE"); }
    }
    private static String required(JsonNode node,String field,int max) {
        JsonNode value=node.path(field);
        if(!value.isTextual() || value.asText().isBlank() || value.asText().length()>max)
            throw new SpeakerFailure(502,"INVALID_MARKET_RESPONSE");
        return value.asText();
    }
    public DeviceAuthorization begin() {
        var node=request(endpoint("device_authorization_endpoint"),"POST",null,
                form(Map.of("client_id",settings.clientId(),"scope",SCOPES)),false);
        String uri=required(node,"verification_uri_complete",2048);
        trusted(uri);
        long expires=node.path("expires_in").asLong(), interval=node.path("interval").asLong(5);
        if(expires<1 || expires>600 || interval<1 || interval>60) throw new SpeakerFailure(502,"INVALID_MARKET_RESPONSE");
        return new DeviceAuthorization(required(node,"device_code",2048),required(node,"user_code",64),uri,expires,interval);
    }
    public Poll poll(String deviceCode) {
        var node=request(endpoint("token_endpoint"),"POST",null,form(Map.of("client_id",settings.clientId(),
                "grant_type","urn:ietf:params:oauth:grant-type:device_code","device_code",deviceCode)),true);
        if(node.has("error")) return new Poll(null,node.path("error").asText());
        return new Poll(tokens(node,true,null),null);
    }
    public Tokens refresh(String refreshToken,String subject) {
        var node=request(endpoint("token_endpoint"),"POST",null,form(Map.of("client_id",settings.clientId(),
                "grant_type","refresh_token","refresh_token",refreshToken)),true);
        if(node.has("error")) {
            if("invalid_grant".equals(node.path("error").asText())) throw new SpeakerFailure(401,"MARKET_REAUTH_REQUIRED");
            throw new SpeakerFailure(503,"MARKET_UNAVAILABLE");
        }
        Tokens tokens=tokens(node,false,subject);
        if(tokens.refresh().equals(refreshToken)) throw new SpeakerFailure(502,"INVALID_MARKET_RESPONSE");
        return tokens;
    }
    private Tokens tokens(JsonNode node,boolean identityRequired,String expectedSubject) {
        long expires=node.path("expires_in").asLong();
        if(expires<1 || expires>300 || !"Bearer".equalsIgnoreCase(node.path("token_type").asText()))
            throw new SpeakerFailure(502,"INVALID_MARKET_RESPONSE");
        String subject=expectedSubject;
        if(identityRequired || node.has("id_token")) {
            try {
                var validator=new IDTokenValidator(new Issuer(settings.issuer()),new ClientID(settings.clientId()),JWSAlgorithm.RS256,
                        JWKSet.parse(request(endpoint("jwks_uri"),"GET",null,null,false).toString()));
                validator.setMaxClockSkew(30);
                var claims=validator.validate(SignedJWT.parse(required(node,"id_token",16384)),null);
                subject=claims.getSubject().getValue();
                if(expectedSubject!=null && !expectedSubject.equals(subject)) throw new IllegalArgumentException();
            } catch(Exception failure) { throw new SpeakerFailure(502,"INVALID_MARKET_IDENTITY"); }
        }
        if(subject==null || subject.isBlank() || subject.length()>255) throw new SpeakerFailure(502,"INVALID_MARKET_IDENTITY");
        return new Tokens(subject,required(node,"access_token",8192),required(node,"refresh_token",8192),
                System.currentTimeMillis()+expires*1000,node.path("scope").asText());
    }
    public JsonNode profile(Tokens tokens) {
        JsonNode node=request(endpoint("userinfo_endpoint"),"GET",tokens.access(),null,false);
        if(!tokens.subject().equals(node.path("sub").asText())) throw new SpeakerFailure(502,"MARKET_IDENTITY_MISMATCH");
        return node;
    }
    private JsonNode business(String path,String method,String access) {
        URI base=issuer();
        JsonNode node=request(base.resolve(path),method,access,null,false);
        if(node.path("code").asInt()!=20000 || !node.path("data").isObject()) throw new SpeakerFailure(502,"INVALID_MARKET_RESPONSE");
        return node.path("data");
    }
    public Credential credential(Tokens tokens) {
        JsonNode data=business("/api/application-credentials","POST",tokens.access());
        if(!tokens.subject().equals(data.path("subject").asText()) || !settings.clientId().equals(data.path("client_id").asText()))
            throw new SpeakerFailure(502,"MARKET_IDENTITY_MISMATCH");
        String base=required(data,"base_url",255); URI uri=trusted(base);
        if(!Set.of("","/").contains(uri.getPath()) || uri.getQuery()!=null) throw new SpeakerFailure(502,"INVALID_MARKET_ENDPOINT");
        Long expires=null;
        if(!data.path("expires_at").isMissingNode() && !data.path("expires_at").isNull()) {
            try { expires=java.time.Instant.parse(data.path("expires_at").asText()).toEpochMilli(); }
            catch(Exception invalid) { throw new SpeakerFailure(502,"INVALID_MARKET_RESPONSE"); }
            if(expires<=System.currentTimeMillis()) throw new SpeakerFailure(502,"MARKET_CREDENTIAL_EXPIRED");
        }
        return new Credential(required(data,"api_key",8192),base,expires);
    }
    public java.math.BigDecimal balance(Tokens tokens) {
        JsonNode data=business("/api/application-balance","GET",tokens.access());
        if(!tokens.subject().equals(data.path("subject").asText()) || !data.path("balance").isNumber())
            throw new SpeakerFailure(502,"INVALID_MARKET_RESPONSE");
        var value=data.path("balance").decimalValue();
        if(value.precision()>30 || value.scale()>8 || value.precision()-value.scale()>22)
            throw new SpeakerFailure(502,"INVALID_MARKET_RESPONSE");
        return value;
    }
}
