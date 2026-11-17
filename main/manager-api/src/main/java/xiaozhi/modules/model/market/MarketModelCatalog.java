package xiaozhi.modules.model.market;

import java.net.*;
import java.net.http.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.time.Duration;
import java.util.*;
import com.fasterxml.jackson.databind.*;
import com.fasterxml.jackson.databind.node.ObjectNode;
import org.springframework.stereotype.Service;
import xiaozhi.modules.speaker.SpeakerFailure;

/** Server application token only; never uses a user's resource Key. */
@Service
public class MarketModelCatalog {
    private final MarketCatalogSettings settings;
    private final ObjectMapper json=new ObjectMapper();
    private final HttpClient http=HttpClient.newBuilder().followRedirects(HttpClient.Redirect.NEVER)
            .connectTimeout(Duration.ofSeconds(5)).build();
    private String access;
    private long expiresAt;
    public MarketModelCatalog(MarketCatalogSettings settings) { this.settings=settings; }
    private URI issuer() {
        try {
            var uri=URI.create(settings.issuer());
            boolean known="https://aimarket.hncig.cn/oidc".equals(settings.issuer());
            boolean fixture="http".equals(uri.getScheme()) && "127.0.0.1".equals(uri.getHost()) && "/oidc".equals(uri.getPath());
            if((!known && !fixture) || uri.getUserInfo()!=null || uri.getQuery()!=null || uri.getFragment()!=null) throw new IllegalArgumentException();
            return uri;
        } catch(Exception e) { throw new SpeakerFailure(503,"MARKET_CATALOG_CONFIG_REQUIRED"); }
    }
    private JsonNode send(URI uri,String auth,String form) {
        try {
            var builder=HttpRequest.newBuilder(uri).timeout(Duration.ofSeconds(12)).header("Accept","application/json");
            if(auth!=null) builder.header("Authorization",auth);
            if(form!=null) builder.header("Content-Type","application/x-www-form-urlencoded");
            var request=form==null?builder.GET().build():builder.POST(HttpRequest.BodyPublishers.ofString(form)).build();
            var response=http.send(request,HttpResponse.BodyHandlers.ofInputStream());
            byte[] bytes;
            try(var body=response.body()) { bytes=body.readNBytes(2097153); }
            if(response.statusCode()==401) throw new SpeakerFailure(401,"MARKET_CATALOG_AUTH_REQUIRED");
            if(response.statusCode()==403) throw new SpeakerFailure(403,"MARKET_CATALOG_FORBIDDEN");
            if(response.statusCode()/100!=2 || bytes.length>2097152) throw new SpeakerFailure(503,"MARKET_CATALOG_UNAVAILABLE");
            var data=json.readTree(bytes);
            if(data==null || !data.isObject()) throw new IllegalArgumentException();
            return data;
        } catch(SpeakerFailure failure) { throw failure; }
        catch(InterruptedException failure) { Thread.currentThread().interrupt(); throw new SpeakerFailure(503,"MARKET_CATALOG_UNAVAILABLE"); }
        catch(Exception failure) { throw new SpeakerFailure(503,"MARKET_CATALOG_UNAVAILABLE"); }
    }
    private synchronized String token() {
        if(access!=null && expiresAt>System.currentTimeMillis()+15000) return access;
        if(settings.clientId().isBlank() || settings.secretFile().isBlank()) throw new SpeakerFailure(503,"MARKET_CATALOG_CONFIG_REQUIRED");
        try {
            var discovery=send(URI.create(issuer()+"/.well-known/openid-configuration"),null,null);
            var endpoint=URI.create(discovery.path("token_endpoint").asText());
            var origin=issuer();
            if(!settings.issuer().equals(discovery.path("issuer").asText())
                    || !Objects.equals(origin.getScheme(),endpoint.getScheme()) || !Objects.equals(origin.getHost(),endpoint.getHost())
                    || origin.getPort()!=endpoint.getPort() || endpoint.getUserInfo()!=null || endpoint.getFragment()!=null)
                throw new IllegalArgumentException();
            byte[] raw=Files.readAllBytes(Path.of(settings.secretFile()));
            if(raw.length>4096) throw new IllegalArgumentException();
            String secret=new String(raw,StandardCharsets.UTF_8).strip();
            if(secret.isEmpty()) throw new IllegalArgumentException();
            String basic=Base64.getEncoder().encodeToString((URLEncoder.encode(settings.clientId(),StandardCharsets.UTF_8)+":"
                    +URLEncoder.encode(secret,StandardCharsets.UTF_8)).getBytes(StandardCharsets.UTF_8));
            var result=send(endpoint,"Basic "+basic,"grant_type=client_credentials&scope=models%3Aread");
            long lifetime=result.path("expires_in").asLong();
            if(!"Bearer".equalsIgnoreCase(result.path("token_type").asText()) || !Arrays.asList(result.path("scope").asText().split(" ")).contains("models:read")
                    || lifetime<30 || lifetime>3600 || result.path("access_token").asText().isBlank()) throw new IllegalArgumentException();
            access=result.path("access_token").asText(); expiresAt=System.currentTimeMillis()+lifetime*1000;
            return access;
        } catch(SpeakerFailure failure) { throw failure; }
        catch(Exception failure) { throw new SpeakerFailure(503,"MARKET_CATALOG_CONFIG_REQUIRED"); }
    }
    private JsonNode page(int page) {
        URI url=issuer().resolve("/api/application-models?page="+page+"&pageSize=100");
        try { return send(url,"Bearer "+token(),null); }
        catch(SpeakerFailure failure) {
            if(failure.status!=401) throw failure;
            synchronized(this) { access=null; expiresAt=0; }
            return send(url,"Bearer "+token(),null);
        }
    }
    public List<JsonNode> list() {
        var result=new ArrayList<JsonNode>();
        int pages=1,total=-1;
        for(int index=1;index<=pages;index++) {
            var response=page(index); var data=response.path("data");
            if(response.path("code").asInt()!=20000 || !data.path("items").isArray()) throw new SpeakerFailure(502,"INVALID_MARKET_CATALOG");
            if(index==1) { pages=data.path("totalPages").asInt(1); total=data.path("total").asInt(-1); }
            if(pages<0 || pages>100 || total<0 || total>10000) throw new SpeakerFailure(502,"INVALID_MARKET_CATALOG");
            for(var item:data.path("items")) {
                if(!item.path("publicId").isTextual() || item.path("publicId").asText().isBlank() || item.path("publicId").asText().length()>255)
                    throw new SpeakerFailure(502,"INVALID_MARKET_CATALOG");
                ObjectNode clean=json.createObjectNode();
                for(String field:new String[]{"publicId","name","protocol","capabilities","verifiedCapabilities","verifiedMethods"})
                    if(item.has(field)) clean.set(field,item.get(field));
                result.add(clean);
            }
        }
        if(result.size()!=total || result.stream().map(m->m.path("publicId").asText()).distinct().count()!=result.size())
            throw new SpeakerFailure(502,"INVALID_MARKET_CATALOG");
        return List.copyOf(result);
    }
}
