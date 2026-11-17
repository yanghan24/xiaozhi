package xiaozhi.modules.model.market;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

@Component
public record MarketCatalogSettings(
        @Value("${market.catalog.issuer:https://aimarket.hncig.cn/oidc}") String issuer,
        @Value("${market.catalog.client-id:}") String clientId,
        @Value("${market.catalog.client-secret-file:}") String secretFile) {
    @Override public String toString() { return "MarketCatalogSettings[redacted]"; }
}
