package xiaozhi.modules.model.market;

import java.net.URI;
import java.time.Duration;
import java.util.*;
import java.util.concurrent.locks.Lock;
import com.google.common.util.concurrent.Striped;
import org.springframework.stereotype.Service;
import xiaozhi.modules.speaker.*;

/** Per-customer resource credentials. Identity binding never depends on provisioning. */
@Service
public class MarketResourceCredentials {
    static final long VERIFICATION_MS = Duration.ofMinutes(10).toMillis();
    private static final long CHECK_LEASE_MS = Duration.ofMinutes(1).toMillis();
    private static final long RETRY_DELAY_MS = Duration.ofMinutes(1).toMillis();
    private final SpeakerGrants grants;
    private final SpeakerMarket market;
    private final SpeakerCrypto crypto;
    private final SpeakerSettings settings;
    private final Striped<Lock> checks = Striped.lazyWeakLock(256);

    public record Current(String generation, SpeakerMarket.Credential credential) {
        public Map<String,String> forVoice() {
            return Map.of("generation",generation,"api_key",credential.key());
        }
    }
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
        return current(customer,null).credential();
    }

    public Current current(String customer, String deniedGeneration) {
        if(customer==null || customer.isBlank()) throw new SpeakerFailure(401,"MARKET_USER_REQUIRED");
        var grant=grants.grant(customer);
        long now=System.currentTimeMillis();
        requireAuthorization(grant,now);
        var current=verified(grant);
        if(current!=null && recent(grant,now) && !Objects.equals(deniedGeneration,grant.get("generation"))) return current;

        var lock=checks.get(customer);
        lock.lock();
        try {
            return check(customer,deniedGeneration);
        } finally {
            lock.unlock();
        }
    }

    private Current check(String customer, String deniedGeneration) {
        var grant=grants.grant(customer);
        long now=System.currentTimeMillis();
        requireAuthorization(grant,now);
        if("blocked".equals(grant.get("credential_status")))
            throw new SpeakerFailure(401,"MARKET_MODEL_KEY_REJECTED");
        String generation=grant.get("generation").toString();
        boolean reported=generation.equals(deniedGeneration);
        var previous=verified(grant);
        if(previous!=null && recent(grant,now) && !reported) return previous;
        long claim=now+CHECK_LEASE_MS;
        if(!grants.claimCredentialCheck(customer,generation,now,claim))
            return usableDuringCheck(grant,previous,reported);
        boolean allowPrevious=!reported && previous!=null;
        try {
            var credential=cached(grant);
            if(credential!=null) {
                if(market.credentialAccepted(credential)) {
                    save(customer,generation,claim,credential,false);
                    if(reported) throw new SpeakerFailure(403,"MARKET_MODEL_FORBIDDEN");
                    return currentVerified(customer);
                }
                allowPrevious=false;
                if(!grants.markCredentialRejected(customer,generation,claim,false)) return currentVerified(customer);
            }
            credential=provision(customer);
            if(!market.credentialAccepted(credential)) {
                grants.markCredentialRejected(customer,generation,claim,true);
                throw new SpeakerFailure(401,"MARKET_MODEL_KEY_REJECTED");
            }
            save(customer,generation,claim,credential,true);
            return currentVerified(customer);
        } catch(SpeakerFailure failure) {
            grants.finishCredentialCheck(customer,generation,claim,System.currentTimeMillis()+RETRY_DELAY_MS);
            // Short provider outages do not discard a previously accepted Key.
            if(allowPrevious && (failure.status>=500 || failure.status==429)) {
                var latest=grants.grant(customer);
                requireAuthorization(latest,System.currentTimeMillis());
                var latestVerified=verified(latest);
                if(latestVerified!=null) return latestVerified;
            }
            throw failure;
        }
    }

    private SpeakerMarket.Credential provision(String customer) {
        SpeakerMarket.Tokens tokens;
        boolean renewed=false;
        try {
            tokens=grants.currentTokens(customer);
        } catch(SpeakerFailure failure) {
            if(!"MARKET_TOKEN_PENDING".equals(failure.getMessage())) throw failure;
            tokens=grants.renewCredentialTokens(customer,null);
            renewed=true;
        }
        try {
            return market.credential(tokens);
        } catch(SpeakerFailure failure) {
            if(!"MARKET_AUTH_REQUIRED".equals(failure.getMessage())) throw failure;
            if(!renewed) {
                try {
                    return market.credential(grants.renewCredentialTokens(customer,tokens.access()));
                } catch(SpeakerFailure afterRefresh) {
                    if(afterRefresh.status!=401 || !"MARKET_AUTH_REQUIRED".equals(afterRefresh.getMessage()))
                        throw afterRefresh;
                }
            }
            throw new SpeakerFailure(503,"MARKET_CREDENTIAL_UNAVAILABLE");
        }
    }

    private void save(String customer, String generation, long claim, SpeakerMarket.Credential credential, boolean replace) {
        if(!grants.saveVerifiedCredential(customer,generation,claim,credential,replace))
            throw new SpeakerFailure(503,"MARKET_CREDENTIAL_PENDING");
    }

    private Current currentVerified(String customer) {
        var grant=grants.grant(customer);
        requireAuthorization(grant,System.currentTimeMillis());
        return usableDuringCheck(grant,verified(grant),false);
    }

    private static Current usableDuringCheck(Map<String,Object> grant, Current previous, boolean reported) {
        if(previous!=null && !reported) return previous;
        if(Set.of("rejected","blocked").contains(grant.get("credential_status")))
            throw new SpeakerFailure(401,"MARKET_MODEL_KEY_REJECTED");
        throw new SpeakerFailure(503,"MARKET_CREDENTIAL_PENDING");
    }

    private static void requireAuthorization(Map<String,Object> grant,long now) {
        if(!SpeakerGrants.usable(grant,now)) throw new SpeakerFailure(403,"MARKET_AUTH_REQUIRED");
    }

    private Current verified(Map<String,Object> grant) {
        var credential=checkedAt(grant)>0 ? cached(grant) : null;
        return credential==null ? null : new Current(grant.get("generation").toString(),credential);
    }

    private static boolean recent(Map<String,Object> grant,long now) {
        return "active".equals(grant.get("credential_status")) && checkedAt(grant)>now-VERIFICATION_MS;
    }

    private static long checkedAt(Map<String,Object> grant) {
        return grant.get("credential_verified_at_ms") instanceof Number checked ? checked.longValue() : 0;
    }
}
