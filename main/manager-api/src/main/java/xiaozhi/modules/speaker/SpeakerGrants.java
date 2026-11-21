package xiaozhi.modules.speaker;

import java.math.BigDecimal;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.locks.Lock;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.util.concurrent.Striped;
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
    private final JdbcTemplate db;
    private final SpeakerSettings settings;
    private final SpeakerCrypto crypto;
    private final SpeakerMarket market;
    private final TransactionTemplate transactions;
    private final Striped<Lock> balanceLocks = Striped.lazyWeakLock(64);
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
            db.update("INSERT INTO speaker_grant(customer_id,client_id,access_cipher,refresh_cipher,access_expires_at_ms,token_updated_at_ms,generation) "
                    + "VALUES (?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE access_cipher=VALUES(access_cipher),refresh_cipher=VALUES(refresh_cipher),"
                    + "access_expires_at_ms=VALUES(access_expires_at_ms),token_updated_at_ms=VALUES(token_updated_at_ms),token_status='active',token_version=token_version+1",
                    id,settings.clientId(),crypto.encrypt(tokens.access()),crypto.encrypt(tokens.refresh()),tokens.expiresAt(),now,token());
            db.update("UPDATE speaker_login SET customer_id=? WHERE id=? AND status='polling'",id,loginId);
            return id;
        });
    }
    private static String text(JsonNode profile,String field,int max) {
        var value=profile.path(field);
        return value.isTextual() && !value.asText().isBlank() ? value.asText().substring(0,Math.min(max,value.asText().length())) : null;
    }
    public void saveCredential(String customer,SpeakerMarket.Tokens tokens,SpeakerMarket.Credential credential) {
        // A late result from an older login must not replace credentials from a later login.
        transactions.executeWithoutResult(tx -> {
            var row=db.queryForMap("SELECT * FROM speaker_grant WHERE customer_id=? AND client_id=? FOR UPDATE",customer,settings.clientId());
            if(!tokens.access().equals(crypto.decrypt(row.get("access_cipher").toString()))) return;
            boolean changed=!"active".equals(row.get("credential_status")) || row.get("credential_cipher")==null || !credential.key().equals(crypto.decrypt(row.get("credential_cipher").toString()));
            db.update("UPDATE speaker_grant SET credential_cipher=?,credential_base=?,credential_expires_at_ms=?,credential_status='active',generation=? "
                    + "WHERE customer_id=? AND client_id=?",crypto.encrypt(credential.key()),credential.baseUrl(),credential.expiresAt(),
                    changed?token():row.get("generation"),customer,settings.clientId());
        });
    }
    public Map<String,Object> grant(String customer) {
        var rows=db.queryForList("SELECT g.*,c.subject FROM speaker_grant g JOIN speaker_customer c ON c.id=g.customer_id "
                + "WHERE g.customer_id=? AND g.client_id=? AND c.issuer=?",customer,settings.clientId(),settings.issuer());
        return rows.isEmpty()?null:rows.getFirst();
    }
    public static boolean usable(Map<String,Object> grant,long now) {
        // Identity authorization is independent of resource keys and short token expiry.
        return grant!=null && java.util.Set.of("active","refreshing","uncertain").contains(grant.get("token_status"));
    }
    private SpeakerMarket.Tokens tokens(Map<String,Object> grant) {
        return new SpeakerMarket.Tokens(grant.get("subject").toString(),crypto.decrypt(grant.get("access_cipher").toString()),
                grant.get("refresh_cipher")==null?null:crypto.decrypt(grant.get("refresh_cipher").toString()),number(grant,"access_expires_at_ms"),"");
    }
    public SpeakerMarket.Tokens currentTokens(String customer) {
        var grant=grant(customer);
        if(grant==null || number(grant,"access_expires_at_ms")<=System.currentTimeMillis())
            throw new SpeakerFailure(503,"MARKET_TOKEN_PENDING");
        return tokens(grant);
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
        if(grant==null || number(grant,"access_expires_at_ms")<=now || !"active".equals(grant.get("token_status"))) {
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
    @Scheduled(fixedDelayString="${speaker.refresh-delay-ms:10000}")
    public void refreshDue() {
        if(!settings.enabled() || settings.clientId().isBlank()) return;
        long now=System.currentTimeMillis();
        // A crashed in-flight rotation cannot be replayed. Only token state changes, never device permission or Key state.
        db.update("UPDATE speaker_grant SET token_status='uncertain',refresh_cipher=NULL WHERE client_id=? AND token_status='refreshing' AND token_updated_at_ms<?",
                settings.clientId(),now-30_000);
        for(var grant:db.queryForList("SELECT g.*,c.subject FROM speaker_grant g JOIN speaker_customer c ON c.id=g.customer_id "
                + "WHERE g.client_id=? AND c.issuer=? AND g.token_status='active' AND g.access_expires_at_ms<=? LIMIT 10",settings.clientId(),settings.issuer(),now+30_000))
            refresh(grant);
    }
    void refresh(Map<String,Object> grant) {
        Object customer=grant.get("customer_id"), version=grant.get("token_version");
        if(grant.get("refresh_cipher")==null) return;
        if(db.update("UPDATE speaker_grant SET token_status='refreshing',token_updated_at_ms=? WHERE customer_id=? AND client_id=? "
                + "AND token_version=? AND token_status='active'",System.currentTimeMillis(),customer,settings.clientId(),version)!=1) return;
        try {
            var old=tokens(grant);
            var next=market.refresh(old.refresh(),old.subject());
            String access=crypto.encrypt(next.access()),refresh=crypto.encrypt(next.refresh());
            // Persist the same response on a DB retry; never repeat the external refresh request.
            for(int attempt=0;attempt<3;attempt++) {
                try {
                    db.update("UPDATE speaker_grant SET access_cipher=?,refresh_cipher=?,access_expires_at_ms=?,token_status='active',token_updated_at_ms=? "
                            + "WHERE customer_id=? AND client_id=? AND token_version=? AND token_status='refreshing'",access,refresh,next.expiresAt(),
                            System.currentTimeMillis(),customer,settings.clientId(),version);
                    return;
                } catch(org.springframework.dao.DataAccessException failure) { if(attempt==2) throw failure; }
            }
        } catch(Exception failure) {
            String state=failure instanceof SpeakerFailure f && "MARKET_REAUTH_REQUIRED".equals(f.getMessage())?"reauth_required":"uncertain";
            db.update("UPDATE speaker_grant SET token_status=?,refresh_cipher=NULL,token_updated_at_ms=? "
                    + "WHERE customer_id=? AND client_id=? AND token_version=? AND token_status='refreshing'",state,System.currentTimeMillis(),customer,settings.clientId(),version);
        }
    }
}
