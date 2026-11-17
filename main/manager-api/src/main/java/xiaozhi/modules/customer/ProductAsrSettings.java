package xiaozhi.modules.customer;

import java.util.Map;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

/** One global selection; credentials never enter generic parameter/config responses. */
@Service
public class ProductAsrSettings {
    private final JdbcTemplate db;
    private final CustomerOidcConfig secrets;
    public ProductAsrSettings(JdbcTemplate db, CustomerOidcConfig secrets) {
        this.db = db; this.secrets = secrets;
    }
    public record Update(String provider, Long revision, String appId, String apiKey, String apiSecret) {}

    private Map<String,Object> row() {
        return db.queryForMap("SELECT * FROM product_asr_settings WHERE id=1");
    }
    private Map<String,Object> display(Map<String,Object> row) {
        return Map.of("provider", row.get("provider"), "revision", row.get("revision"),
                "xfyunConfigured", row.get("credential_cipher") != null);
    }
    public Map<String,Object> get() { return display(row()); }

    @Transactional
    public Map<String,Object> save(Update update) {
        if (update == null || update.provider() == null ||
                !java.util.Set.of("market", "xfyun").contains(update.provider()) || update.revision() == null)
            throw new CustomerFailure(400, "INVALID_ASR_SETTINGS");
        var current = db.queryForMap("SELECT * FROM product_asr_settings WHERE id=1 FOR UPDATE");
        if (((Number) current.get("revision")).longValue() != update.revision())
            throw new CustomerFailure(409, "ASR_SETTINGS_CHANGED");
        String cipher = (String) current.get("credential_cipher");
        boolean changing = !blank(update.appId()) || !blank(update.apiKey()) || !blank(update.apiSecret());
        if (changing) {
            if (!valid(update.appId(), 64) || !valid(update.apiKey(), 256) || !valid(update.apiSecret(), 256))
                throw new CustomerFailure(400, "COMPLETE_XFYUN_CREDENTIALS_REQUIRED");
            cipher = secrets.encrypt(update.appId() + "\n" + update.apiKey() + "\n" + update.apiSecret());
        }
        if ("xfyun".equals(update.provider()) && cipher == null)
            throw new CustomerFailure(400, "XFYUN_NOT_CONFIGURED");
        db.update("UPDATE product_asr_settings SET provider=?,credential_cipher=?,revision=revision+1 WHERE id=1",
                update.provider(), cipher);
        return get();
    }

    Map<String,Object> access() {
        var row = row();
        if ("market".equals(row.get("provider"))) return Map.of("provider", "market");
        if (!"xfyun".equals(row.get("provider")) || row.get("credential_cipher") == null)
            throw new CustomerFailure(503, "ASR_UNAVAILABLE");
        String[] credential = secrets.decrypt(row.get("credential_cipher").toString()).split("\n", -1);
        if (credential.length != 3) throw new CustomerFailure(503, "ASR_UNAVAILABLE");
        return Map.of("provider", "xfyun", "app_id", credential[0], "api_key", credential[1], "api_secret", credential[2]);
    }
    private static boolean blank(String text) { return text == null || text.isBlank(); }
    private static boolean valid(String text, int max) {
        return text != null && text.length() <= max && text.matches("[A-Za-z0-9_+/=.-]+");
    }
}
