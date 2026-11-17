package xiaozhi.modules.devicecontrol;

import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.HexFormat;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

/** Short-lived operation/artifact bearer capability; never returned to the browser. */
@Component
public class ProductOtaDownloads {
    private final String origin,key;
    public ProductOtaDownloads(@Value("${product.ota.download-origin:}") String origin,
            @Value("${product.ota.download-key:}") String key) { this.origin=origin; this.key=key; }
    private void configured() {
        try {
            URI uri=URI.create(origin);
            if(!"https".equals(uri.getScheme()) || uri.getHost()==null || uri.getUserInfo()!=null
                    || !uri.getRawPath().isEmpty() || uri.getQuery()!=null || uri.getFragment()!=null
                    || key.getBytes(StandardCharsets.UTF_8).length<32) throw new IllegalArgumentException();
        } catch(Exception invalid) { throw new ControlFailure(503,"OTA_DOWNLOAD_UNAVAILABLE"); }
    }
    public String signature(String operation,String sha,long expires) {
        configured();
        try {
            Mac hmac=Mac.getInstance("HmacSHA256");
            hmac.init(new SecretKeySpec(key.getBytes(StandardCharsets.UTF_8),"HmacSHA256"));
            return HexFormat.of().formatHex(hmac.doFinal((operation+"\n"+sha+"\n"+expires).getBytes(StandardCharsets.UTF_8)));
        } catch(Exception unavailable) { throw new ControlFailure(503,"OTA_DOWNLOAD_UNAVAILABLE"); }
    }
    public String url(String operation,String sha,long expires) {
        return origin+"/xiaozhi/ota-artifacts/"+operation+"/"+sha+".bin?expires="+expires+"&signature="+signature(operation,sha,expires);
    }
}
