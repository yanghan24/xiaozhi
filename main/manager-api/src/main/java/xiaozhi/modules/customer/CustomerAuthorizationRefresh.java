package xiaozhi.modules.customer;

import org.springframework.beans.factory.annotation.Qualifier;

import java.time.Clock;
import java.util.Map;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

/** Database claims coordinate all instances; no process-local refresh locks. */
@Component
final class CustomerAuthorizationRefresh {
    private final JdbcTemplate db;
    private final CustomerOidcConfig config;
    private final CustomerMarket market;
    private final Clock clock;
    private final org.springframework.transaction.support.TransactionTemplate transactions;
    private final CustomerVoicePermissions voice;
    CustomerAuthorizationRefresh(JdbcTemplate db, CustomerOidcConfig config, CustomerMarket market, @Qualifier("customerTime") Clock clock,
            org.springframework.transaction.PlatformTransactionManager manager,CustomerVoicePermissions voice) {
        this.db = db; this.config = config; this.market = market; this.clock = clock;
        this.voice=voice;transactions=new org.springframework.transaction.support.TransactionTemplate(manager);
        transactions.setIsolationLevel(org.springframework.transaction.TransactionDefinition.ISOLATION_READ_COMMITTED);
    }
    private void locked(Runnable work) {
        transactions.executeWithoutResult(tx->{
            db.queryForObject("SELECT id FROM customer_identity_barrier WHERE id=1 FOR UPDATE",Integer.class);
            work.run();
        });
    }
    @Scheduled(fixedDelayString="${customer.oidc.refresh-delay-ms:10000}")
    void refreshDue() {
        if (config.issuer == null) return;
        try {
            // A crashed or disconnected claimant must never replay its old rotating token.
            locked(()->{
                for(var stale:db.queryForList("SELECT * FROM customer_authorization WHERE issuer=? AND status='REFRESHING' AND updated_at_ms<? FOR UPDATE",config.issuer,clock.millis()-30_000)) {
                    db.update("UPDATE customer_authorization SET status='REFRESH_UNCERTAIN',refresh_token='' WHERE subject_key=? AND generation=? AND status='REFRESHING'",stale.get("subject_key"),stale.get("generation"));
                    voice.reconcileSubject(stale.get("issuer").toString(),stale.get("subject").toString());
                }
            });
            for (var grant : db.queryForList("SELECT * FROM customer_authorization WHERE issuer=? AND status='ACTIVE' AND access_expires_at_ms<=? ORDER BY access_expires_at_ms LIMIT 50",
                    config.issuer, clock.millis() + 30_000)) refresh(grant);
        } catch (Exception ignored) {
            // Leave durable claims in place. Do not expose encrypted or plain tokens in logs.
        }
    }
    private void refresh(Map<String,Object> grant) {
        String subject = grant.get("subject_key").toString(), generation = grant.get("generation").toString();
        if (db.update("UPDATE customer_authorization SET status='REFRESHING',refresh_token='',updated_at_ms=? WHERE subject_key=? AND generation=? AND status='ACTIVE' AND access_expires_at_ms=?",
                clock.millis(), subject, generation, grant.get("access_expires_at_ms")) != 1) return;
        CustomerMarket.Refreshed response = null;
        try {
            response = market.refresh(config.decrypt(grant.get("refresh_token").toString()));
            String access = config.encrypt(response.access()), refresh = config.encrypt(response.refresh());
            long expiresAt=response.expiresAt();
            // Retry only saving this SAME successful response. Never reissue the refresh request.
            for (int attempt=0; attempt<3; attempt++) {
                try {
                    locked(()->db.update("UPDATE customer_authorization SET access_token=?,refresh_token=?,access_expires_at_ms=?,status='ACTIVE',updated_at_ms=? WHERE subject_key=? AND generation=? AND status='REFRESHING'",
                            access, refresh, expiresAt, clock.millis(), subject, generation));
                    return;
                } catch (org.springframework.dao.DataAccessException failure) {
                    if (attempt==2) throw failure;
                }
            }
        } catch (Exception failure) {
            boolean unrecoverable = response != null || failure instanceof CustomerFailure error && error.getMessage().equals("REAUTH_REQUIRED");
            String status = unrecoverable ? "REAUTH_REQUIRED" : "REFRESH_UNCERTAIN";
            locked(()->{
                if(db.update("UPDATE customer_authorization SET status=?,updated_at_ms=? WHERE subject_key=? AND generation=? AND status='REFRESHING'",
                    status, clock.millis(), subject, generation)==1)
                    voice.reconcileSubject(grant.get("issuer").toString(),grant.get("subject").toString());
            });
        }
    }
}
