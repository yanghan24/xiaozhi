package xiaozhi.modules.speaker;

import java.math.BigDecimal;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.locks.Lock;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.util.concurrent.Striped;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.dao.DataAccessException;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;
import static xiaozhi.modules.speaker.SpeakerSupport.*;

/** Long-term business credentials are independent of short-lived identity access tokens. */
@Service
public class SpeakerGrants {
    private static final long BALANCE_CACHE_MS = 60_000;
    private static final long BALANCE_RETRY_MS = 15_000;
    private static final long[] REFRESH_RETRY_MS = {2_000, 5_000, 10_000, 30_000, 60_000};
    private static final Logger LOG = LoggerFactory.getLogger(SpeakerGrants.class);
    private final JdbcTemplate db;
    private final SpeakerSettings settings;
    private final SpeakerCrypto crypto;
    private final SpeakerMarket market;
    private final TransactionTemplate transactions;
    private final Striped<Lock> balanceLocks = Striped.lazyWeakLock(64);
    private final Map<RefreshClaim, SpeakerMarket.Tokens> pendingRefreshes = new ConcurrentHashMap<>();

    private record RefreshClaim(String customer, long version) {}
    public SpeakerGrants(JdbcTemplate db,SpeakerSettings settings,SpeakerCrypto crypto,SpeakerMarket market,
            PlatformTransactionManager manager) {
        this.db=db;this.settings=settings;this.crypto=crypto;this.market=market;
        transactions=new TransactionTemplate(manager);
    }
    public String saveIdentity(SpeakerMarket.Tokens tokens,JsonNode profile,String loginId) {
        return transactions.execute(tx -> {
            String id=UUID.randomUUID().toString();long now=System.currentTimeMillis();
            db.update("INSERT INTO speaker_customer(id,issuer,subject,created_at_ms) VALUES (?,?,?,?) "
                    + "ON DUPLICATE KEY UPDATE subject=VALUES(subject)",id,settings.issuer(),tokens.subject(),now);
            var customer=db.queryForMap("SELECT * FROM speaker_customer WHERE issuer=? AND subject=? FOR UPDATE",settings.issuer(),tokens.subject());
            id=customer.get("id").toString();
            String organization=profile.path("organization").isObject()?profile.path("organization").toString():null;
            db.update("UPDATE speaker_customer SET display_name=?,username=?,account_type=?,organization=?,profile_updated_at_ms=? WHERE id=?",
                    text(profile,"name",255),text(profile,"preferred_username",255),text(profile,"account_type",32),organization,now,id);
            db.update("INSERT INTO speaker_grant(customer_id,client_id,access_cipher,refresh_cipher,access_expires_at_ms,token_updated_at_ms,generation,token_scope) "
                    + "VALUES (?,?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE access_cipher=VALUES(access_cipher),refresh_cipher=VALUES(refresh_cipher),"
                    + "access_expires_at_ms=VALUES(access_expires_at_ms),token_updated_at_ms=VALUES(token_updated_at_ms),"
                    + "token_scope=VALUES(token_scope),token_status='active',token_version=token_version+1,token_retry_count=0,token_retry_at_ms=0,token_error=NULL,"
                    + "generation=VALUES(generation),credential_status='pending',credential_verified_at_ms=0,credential_check_after_ms=0",
                    id,settings.clientId(),crypto.encrypt(tokens.access()),crypto.encrypt(tokens.refresh()),tokens.expiresAt(),now,token(),
                    java.util.Objects.toString(tokens.scope(),""));
            db.update("UPDATE speaker_login SET customer_id=? WHERE id=? AND status='polling'",id,loginId);
            return id;
        });
    }
    private static String text(JsonNode profile,String field,int max) {
        var value=profile.path(field);
        return value.isTextual() && !value.asText().isBlank() ? value.asText().substring(0,Math.min(max,value.asText().length())) : null;
    }
    public boolean claimCredentialCheck(String customer, String generation, long now, long until) {
        return db.update("""
                UPDATE speaker_grant SET credential_check_after_ms=?
                WHERE customer_id=? AND client_id=? AND generation=? AND credential_check_after_ms<=?
                  AND token_status IN ('active','refreshing','uncertain','config_error')
                """,until,customer,settings.clientId(),generation,now)==1;
    }

    public boolean markCredentialRejected(String customer, String generation, long claim, boolean recoveryFailed) {
        return db.update("""
                UPDATE speaker_grant SET credential_status=?,credential_verified_at_ms=0
                WHERE customer_id=? AND client_id=? AND generation=? AND credential_check_after_ms=?
                """,recoveryFailed?"blocked":"rejected",customer,settings.clientId(),generation,claim)==1;
    }

    public boolean saveVerifiedCredential(String customer, String generation, long claim,
            SpeakerMarket.Credential credential, boolean replace) {
        return db.update("""
                UPDATE speaker_grant
                SET credential_cipher=?,credential_base=?,credential_expires_at_ms=?,credential_status='active',
                    credential_verified_at_ms=?,credential_check_after_ms=0,generation=?
                WHERE customer_id=? AND client_id=? AND generation=? AND credential_check_after_ms=?
                  AND token_status IN ('active','refreshing','uncertain','config_error')
                """,crypto.encrypt(credential.key()),credential.baseUrl(),credential.expiresAt(),System.currentTimeMillis(),
                replace?token():generation,customer,settings.clientId(),generation,claim)==1;
    }

    public void finishCredentialCheck(String customer, String generation, long claim, long retryAt) {
        db.update("""
                UPDATE speaker_grant SET credential_check_after_ms=?
                WHERE customer_id=? AND client_id=? AND generation=? AND credential_check_after_ms=?
                """,retryAt,customer,settings.clientId(),generation,claim);
    }

    public java.util.List<String> credentialsDue(long now, long verifiedBefore, long seenAfter) {
        return db.queryForList("""
                SELECT g.customer_id FROM speaker_grant g JOIN speaker_customer c ON c.id=g.customer_id
                WHERE g.client_id=? AND c.issuer=? AND g.credential_check_after_ms<=?
                  AND g.credential_verified_at_ms<=? AND g.token_status='active' AND g.credential_status<>'blocked'
                  AND EXISTS (SELECT 1 FROM speaker_device_state s JOIN dc_device d ON d.device_id=s.device_id
                              WHERE s.customer_id=g.customer_id AND s.use_enabled=TRUE AND s.admin_enabled=TRUE
                                AND d.admitted=TRUE AND s.last_seen_at_ms>=?)
                ORDER BY g.credential_verified_at_ms,g.customer_id LIMIT 4
                """,String.class,settings.clientId(),settings.issuer(),now,verifiedBefore,seenAfter);
    }
    public Map<String,Object> grant(String customer) {
        var rows=db.queryForList("SELECT g.*,c.subject FROM speaker_grant g JOIN speaker_customer c ON c.id=g.customer_id "
                + "WHERE g.customer_id=? AND g.client_id=? AND c.issuer=?",customer,settings.clientId(),settings.issuer());
        return rows.isEmpty()?null:rows.getFirst();
    }
    public static boolean usable(Map<String,Object> grant,long now) {
        // Identity authorization is independent of resource keys and short token expiry.
        return grant!=null && java.util.Set.of("active","refreshing","uncertain","config_error").contains(grant.get("token_status"));
    }
    private SpeakerMarket.Tokens tokens(Map<String,Object> grant) {
        return new SpeakerMarket.Tokens(grant.get("subject").toString(),crypto.decrypt(grant.get("access_cipher").toString()),
                grant.get("refresh_cipher")==null?null:crypto.decrypt(grant.get("refresh_cipher").toString()),
                number(grant,"access_expires_at_ms"),java.util.Objects.toString(grant.get("token_scope"),""));
    }
    public SpeakerMarket.Tokens currentTokens(String customer) {
        var grant=grant(customer);
        if(grant==null || number(grant,"access_expires_at_ms")<=System.currentTimeMillis())
            throw new SpeakerFailure(503,"MARKET_TOKEN_PENDING");
        return tokens(grant);
    }

    public SpeakerMarket.Tokens renewCredentialTokens(String customer, String rejectedAccess) {
        var grant=grant(customer);
        long now=System.currentTimeMillis();
        if(!usable(grant,now)) throw new SpeakerFailure(403,"MARKET_AUTH_REQUIRED");
        var previous=tokens(grant);
        if(rejectedAccess!=null && !rejectedAccess.equals(previous.access()) && previous.expiresAt()>now)
            return previous;
        if("active".equals(grant.get("token_status"))) refresh(grant);
        var updated=grant(customer);
        if(!usable(updated,System.currentTimeMillis())) throw new SpeakerFailure(403,"MARKET_AUTH_REQUIRED");
        var current=currentTokens(customer);
        if(current.access().equals(previous.access())) throw new SpeakerFailure(503,"MARKET_TOKEN_PENDING");
        return current;
    }
    public void refreshBalance(String customer) {
        settings.requireEnabled();
        var lock = balanceLocks.get(customer);
        lock.lock();
        try {
            fetchBalance(customer);
        } finally {
            lock.unlock();
        }
    }

    String deviceBalance(String customer) {
        settings.requireEnabled();
        var lock = balanceLocks.get(customer);
        lock.lock();
        try {
            var row = db.queryForMap("SELECT balance,balance_status,balance_checked_at_ms FROM speaker_customer WHERE id=?", customer);
            boolean available = "available".equals(row.get("balance_status"));
            if (row.get("balance_checked_at_ms") instanceof Number checked) {
                long age = System.currentTimeMillis() - checked.longValue();
                if (age >= 0 && age < (available ? BALANCE_CACHE_MS : BALANCE_RETRY_MS)) {
                    return available && row.get("balance") instanceof BigDecimal balance ? balanceText(balance) : null;
                }
            }
            var balance = fetchBalance(customer);
            return balance == null ? null : balanceText(balance);
        } finally {
            lock.unlock();
        }
    }

    private BigDecimal fetchBalance(String customer) {
        var grant=grant(customer);
        long now=System.currentTimeMillis();
        if(!usable(grant,now) || number(grant,"access_expires_at_ms")<=now) {
            db.update("UPDATE speaker_customer SET balance_status='unavailable',balance_checked_at_ms=? WHERE id=?",now,customer);
            return null;
        }
        try {
            var balance=market.balance(tokens(grant));
            now=System.currentTimeMillis();
            db.update("UPDATE speaker_customer SET balance=?,balance_status='available',balance_updated_at_ms=?,balance_checked_at_ms=? WHERE id=?",balance,now,now,customer);
            return balance;
        } catch(SpeakerFailure unavailable) {
            db.update("UPDATE speaker_customer SET balance_status='unavailable',balance_checked_at_ms=? WHERE id=?",System.currentTimeMillis(),customer);
            return null;
        }
    }

    private static String balanceText(BigDecimal balance) {
        return balance.stripTrailingZeros().toPlainString();
    }
    @Scheduled(fixedDelayString="${speaker.refresh-delay-ms:1000}")
    public void refreshDue() {
        if(!settings.enabled() || settings.clientId().isBlank()) return;
        pendingRefreshes.forEach(this::persistRefresh);
        long now=System.currentTimeMillis();
        // An interrupted attempt may already have rotated the token. A late result can
        // still be saved by its owner, but another instance must never replay the old token.
        db.update("UPDATE speaker_grant SET token_status='uncertain',token_error='MARKET_REFRESH_UNCERTAIN' "
                + "WHERE client_id=? AND token_status='refreshing' AND token_updated_at_ms<?",
                settings.clientId(),now-30_000);
        for(var grant:db.queryForList("""
                SELECT g.*,c.subject FROM speaker_grant g JOIN speaker_customer c ON c.id=g.customer_id
                WHERE g.client_id=? AND c.issuer=? AND g.token_status='active' AND g.refresh_cipher IS NOT NULL
                    AND g.access_expires_at_ms<=? AND g.token_retry_at_ms<=?
                ORDER BY GREATEST(g.token_retry_at_ms,g.access_expires_at_ms-30000),g.customer_id LIMIT 10
                """,settings.clientId(),settings.issuer(),now+30_000,now))
            refresh(grant);
    }

    void refresh(Map<String,Object> grant) {
        long now = System.currentTimeMillis();
        long retryAt = number(grant,"token_retry_at_ms");
        if (grant.get("refresh_cipher")==null || retryAt>now) return;
        var claim = new RefreshClaim(grant.get("customer_id").toString(),number(grant,"token_version"));
        if(db.update("""
                UPDATE speaker_grant SET token_status='refreshing',token_updated_at_ms=?
                WHERE customer_id=? AND client_id=? AND token_version=? AND token_retry_at_ms=? AND token_status='active'
                """,now,claim.customer(),settings.clientId(),claim.version(),retryAt)!=1) return;

        SpeakerMarket.Tokens next;
        try {
            var old=tokens(grant);
            next=market.refresh(old.refresh(),old.subject());
        } catch (SpeakerMarket.RefreshRetry failure) {
            int step = (int)Math.min(number(grant,"token_retry_count"),REFRESH_RETRY_MS.length-1);
            long delay = failure.retryAfterMs > 0 ? failure.retryAfterMs : REFRESH_RETRY_MS[step];
            now = System.currentTimeMillis();
            db.update("""
                    UPDATE speaker_grant SET token_status='active', token_updated_at_ms=?, token_retry_at_ms=?,
                        token_retry_count=?, token_error=?
                    WHERE customer_id=? AND client_id=? AND token_version=? AND token_status IN ('refreshing','uncertain')
                    """,now,now+delay,Math.min(step+1,REFRESH_RETRY_MS.length-1),failure.getMessage(),claim.customer(),
                    settings.clientId(),claim.version());
            return;
        } catch(Exception failure) {
            String code = failure instanceof SpeakerFailure ? failure.getMessage() : "MARKET_UNAVAILABLE";
            String status = switch (code) {
                case "MARKET_REAUTH_REQUIRED" -> "reauth_required";
                case "MARKET_CLIENT_CONFIGURATION", "MARKET_REFRESH_CONFIGURATION" -> "config_error";
                default -> "uncertain";
            };
            db.update("""
                    UPDATE speaker_grant SET token_status=?, token_updated_at_ms=?, token_error=?,
                        refresh_cipher=CASE WHEN ? THEN NULL ELSE refresh_cipher END
                    WHERE customer_id=? AND client_id=? AND token_version=? AND token_status IN ('refreshing','uncertain')
                    """,status,System.currentTimeMillis(),"uncertain".equals(status)?"MARKET_REFRESH_UNCERTAIN":code,
                    "reauth_required".equals(status),claim.customer(),settings.clientId(),claim.version());
            return;
        }
        pendingRefreshes.put(claim,next);
        persistRefresh(claim,next);
    }

    private void persistRefresh(RefreshClaim claim, SpeakerMarket.Tokens next) {
        try {
            db.update("""
                    UPDATE speaker_grant SET access_cipher=?, refresh_cipher=?, access_expires_at_ms=?, token_scope=COALESCE(NULLIF(?,''),token_scope),
                        token_status='active', token_updated_at_ms=?, token_version=token_version+1,
                        token_retry_count=0, token_retry_at_ms=0, token_error=NULL
                    WHERE customer_id=? AND client_id=? AND token_version=? AND token_status IN ('refreshing','uncertain')
                    """,crypto.encrypt(next.access()),crypto.encrypt(next.refresh()),next.expiresAt(),next.scope(),System.currentTimeMillis(),
                    claim.customer(),settings.clientId(),claim.version());
            pendingRefreshes.remove(claim,next);
        } catch (DataAccessException | SpeakerFailure failure) {
            LOG.debug("Market refresh returned new tokens. Persistence will be retried.");
        }
    }
}
