package xiaozhi.modules.devicecontrol;

import com.fasterxml.jackson.databind.*;
import java.net.*;
import java.net.http.*;
import java.time.Duration;
import java.util.*;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

/** Dedicated authenticated private transport; no generic MCP or browser-controlled URL. */
@Component
public class ProductOtaBridge {
    private final String endpoint, key;
    private final ObjectMapper json = new ObjectMapper();
    private final HttpClient http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(3))
            .followRedirects(HttpClient.Redirect.NEVER).proxy(new ProxySelector() {
                public List<Proxy> select(URI uri) { return List.of(Proxy.NO_PROXY); }
                public void connectFailed(URI uri, SocketAddress address, java.io.IOException error) { }
            }).build();
    public ProductOtaBridge(@Value("${product.ota.bridge-url:}") String endpoint,
            @Value("${product.ota.bridge-key:}") String key) { this.endpoint=endpoint; this.key=key; }
    public JsonNode call(Map<String,Object> request) {
        if(key.length()<32 || endpoint.isBlank()) throw new ControlFailure(503,"BRIDGE_UNAVAILABLE");
        try {
            URI uri=URI.create(endpoint);
            if(!Set.of("http","https").contains(uri.getScheme()) || uri.getUserInfo()!=null
                    || uri.getQuery()!=null || uri.getFragment()!=null
                    || !uri.getPath().equals("/internal/product-ota/v1/control")) throw new IllegalArgumentException();
            var response=http.send(HttpRequest.newBuilder(uri).timeout(Duration.ofSeconds(45))
                    .header("X-OTA-Service-Key",key).header("Content-Type","application/json")
                    .POST(HttpRequest.BodyPublishers.ofByteArray(json.writeValueAsBytes(request))).build(),
                    HttpResponse.BodyHandlers.ofInputStream());
            try(var body=response.body()) {
                byte[] bytes=body.readNBytes(65537);
                if(response.statusCode()!=200 || bytes.length>65536) throw new IllegalArgumentException();
                var result=json.readTree(bytes);
                if(result==null || !result.isObject() || !result.path("status").isTextual()) throw new IllegalArgumentException();
                return result;
            }
        } catch(InterruptedException interrupted) { Thread.currentThread().interrupt(); throw new ControlFailure(503,"BRIDGE_UNAVAILABLE"); }
        catch(Exception unavailable) { throw new ControlFailure(503,"BRIDGE_UNAVAILABLE"); }
    }
}
