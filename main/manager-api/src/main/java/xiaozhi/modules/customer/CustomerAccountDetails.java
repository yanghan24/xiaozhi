package xiaozhi.modules.customer;

import java.time.Clock;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;

/** Read display data through the owner's current, server-side OIDC authorization. */
@Service
public final class CustomerAccountDetails {
    public record Display(String username, String balance) {}

    private final JdbcTemplate db;
    private final CustomerOidcConfig config;
    private final CustomerMarket market;
    private final Clock clock;

    CustomerAccountDetails(JdbcTemplate db, CustomerOidcConfig config, CustomerMarket market,
            @Qualifier("customerTime") Clock clock) {
        this.db = db;
        this.config = config;
        this.market = market;
        this.clock = clock;
    }

    public Display read(String issuer, String subject) {
        if (issuer == null || !issuer.equals(config.issuer) || subject == null) return new Display(null, null);
        var grants = db.queryForList("SELECT access_token FROM customer_authorization "
                + "WHERE subject_key=? AND issuer=? AND subject=? AND status IN ('ACTIVE','REFRESHING') "
                + "AND access_expires_at_ms>?", CustomerSecrets.hash(issuer + "\n" + subject), issuer, subject, clock.millis());
        if (grants.size() != 1) return new Display(null, null);
        Object token = grants.getFirst().get("access_token");
        if (!(token instanceof String encrypted) || encrypted.isBlank()) return new Display(null, null);
        try {
            return market.accountDisplay(subject, config.decrypt(encrypted));
        } catch (RuntimeException unavailable) {
            return new Display(null, null);
        }
    }
}
