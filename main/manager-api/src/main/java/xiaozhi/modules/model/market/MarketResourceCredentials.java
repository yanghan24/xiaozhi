package xiaozhi.modules.model.market;

import java.net.URI;
import java.util.*;
import org.springframework.stereotype.Service;
import xiaozhi.modules.speaker.*;

/** Per-customer resource credentials. Identity binding never depends on provisioning. */
@Service
public class MarketResourceCredentials {
    private final SpeakerGrants grants;
    private final SpeakerMarket market;
    private final SpeakerCrypto crypto;
    private final SpeakerSettings settings;
    public MarketResourceCredentials(SpeakerGrants grants,SpeakerMarket market,SpeakerCrypto crypto,SpeakerSettings settings) {
        this.grants=grants;this.market=market;this.crypto=crypto;this.settings=settings;
    }
    private SpeakerMarket.Credential cached(Map<String,Object> grant) {
        if(grant==null || !"active".equals(grant.get("credential_status")) || grant.get("credential_cipher")==null) return null;
        Object expires=grant.get("credential_expires_at_ms");
        if(expires instanceof Number n && n.longValue()<=System.currentTimeMillis()) return null;
        try {
            String base=Objects.toString(grant.get("credential_base"),"");var uri=URI.create(base);var issuer=URI.create(settings.issuer());
            if(!Objects.equals(uri.getScheme(),issuer.getScheme()) || !Objects.equals(uri.getHost(),issuer.getHost())
                    || uri.getPort()!=issuer.getPort() || uri.getUserInfo()!=null || uri.getQuery()!=null || uri.getFragment()!=null
                    || !Set.of("","/").contains(uri.getPath())) throw new IllegalArgumentException();
            return new SpeakerMarket.Credential(crypto.decrypt(grant.get("credential_cipher").toString()),base,expires==null?null:((Number)expires).longValue());
        } catch(Exception e) { throw new SpeakerFailure(503,"INVALID_MARKET_CREDENTIAL"); }
    }
    public SpeakerMarket.Credential forCustomer(String customer) {
        if(customer==null || customer.isBlank()) throw new SpeakerFailure(401,"MARKET_USER_REQUIRED");
        var grant=grants.grant(customer);
        if(!SpeakerGrants.usable(grant,System.currentTimeMillis())) throw new SpeakerFailure(403,"MARKET_AUTH_REQUIRED");
        var credential=cached(grant);
        if(credential!=null) return credential;
        var tokens=grants.currentTokens(customer);
        credential=market.credential(tokens);
        grants.saveCredential(customer,tokens,credential);
        var stored=cached(grants.grant(customer));
        if(stored==null) throw new SpeakerFailure(503,"MARKET_CREDENTIAL_PENDING");
        return stored;
    }
}
