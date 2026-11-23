package xiaozhi.modules.speaker;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;

@Service
public class SpeakerThirdPartyServices {
    private static final TypeReference<List<Endpoint>> ENDPOINTS = new TypeReference<>() {};
    private static final Set<String> AUTH_MODES = Set.of("none", "query", "header", "bearer", "qweather_jwt");

    public record Endpoint(String path, String purpose) {}
    public record Configuration(String name, String url, List<Endpoint> endpoints, String authMode,
                                String authName, String credential, boolean clearCredential,
                                boolean enabled, long version, String jwtIssuer, String jwtProject,
                                String jwtKeyId, String defaultCity) {
        public Configuration(String name, String url, List<Endpoint> endpoints, String authMode, String authName,
                             String credential, boolean clearCredential, boolean enabled, long version) {
            this(name, url, endpoints, authMode, authName, credential, clearCredential, enabled, version,
                    null, null, null, null);
        }
        @Override public String toString() { return "ThirdPartyConfiguration[redacted]"; }
    }
    public record Entry(String id, String name, String url, List<Endpoint> endpoints, String authMode,
                        String authName, boolean hasCredential, boolean enabled, long version,
                        String jwtIssuer, String jwtProject, String jwtKeyId, String defaultCity) {}

    record AmapConfiguration(long version, String credential) {
        @Override public String toString() { return "AmapConfiguration[redacted]"; }
    }

    record QWeatherConfiguration(long version, String url, String issuer, String project, String keyId,
                                 String defaultCity, String privateKey) {
        @Override public String toString() { return "QWeatherConfiguration[redacted]"; }
    }

    private static final String QWEATHER_READY = """
            FROM speaker_third_party_service WHERE id='qweather' AND enabled=TRUE AND auth_mode='qweather_jwt'
              AND credential_cipher IS NOT NULL AND jwt_issuer IS NOT NULL AND jwt_project IS NOT NULL
              AND jwt_key_id IS NOT NULL
            """;

    private final JdbcTemplate db;
    private final ObjectMapper json;
    private final SpeakerCrypto crypto;

    public SpeakerThirdPartyServices(JdbcTemplate db, ObjectMapper json, SpeakerCrypto crypto) {
        this.db = db;
        this.json = json;
        this.crypto = crypto;
    }

    public List<Entry> list() {
        return db.query("""
                SELECT id, display_name, base_url, endpoints, auth_mode, auth_name,
                       credential_cipher IS NOT NULL AS has_credential, enabled, version,
                       jwt_issuer, jwt_project, jwt_key_id, default_city
                FROM speaker_third_party_service ORDER BY id
                """, (row, index) -> new Entry(row.getString("id"), row.getString("display_name"),
                row.getString("base_url"), readEndpoints(row.getString("endpoints")), row.getString("auth_mode"),
                row.getString("auth_name"), row.getBoolean("has_credential"), row.getBoolean("enabled"),
                row.getLong("version"),
                row.getString("jwt_issuer"), row.getString("jwt_project"), row.getString("jwt_key_id"),
                row.getString("default_city")));
    }

    boolean qweatherReady() {
        return Boolean.TRUE.equals(db.queryForObject("SELECT EXISTS(SELECT 1 " + QWEATHER_READY + ")", Boolean.class));
    }

    Optional<QWeatherConfiguration> qweatherConfiguration() {
        return db.query("SELECT version, base_url, jwt_issuer, jwt_project, jwt_key_id, default_city, credential_cipher "
                + QWEATHER_READY, (row, index) -> new QWeatherConfiguration(row.getLong(1), row.getString(2),
                row.getString(3), row.getString(4), row.getString(5), row.getString(6),
                crypto.decryptThirdPartyCredential("qweather", row.getString(7)))).stream().findFirst();
    }

    Optional<AmapConfiguration> amapConfiguration() {
        return db.query("""
                SELECT version, credential_cipher FROM speaker_third_party_service
                WHERE id='amap' AND enabled=TRUE AND credential_cipher IS NOT NULL
                  AND base_url IN ('https://restapi.amap.com', 'https://restapi.amap.com/')
                  AND auth_mode='query' AND auth_name='key'
                  AND JSON_CONTAINS(endpoints, '{"path": "/v3/ip"}')
                """, (row, index) -> new AmapConfiguration(row.getLong("version"),
                crypto.decryptThirdPartyCredential("amap", row.getString("credential_cipher"))))
                .stream().findFirst();
    }

    @org.springframework.transaction.annotation.Transactional
    public void save(String id, Configuration configuration) {
        if (configuration == null || !validText(configuration.name(), 128)
                || configuration.authMode() == null || !AUTH_MODES.contains(configuration.authMode())
                || configuration.endpoints() == null || configuration.endpoints().size() > 16) {
            throw invalid();
        }
        String url = normalizeUrl(configuration.url(), configuration.enabled());
        var paths = new HashSet<String>();
        for (Endpoint endpoint : configuration.endpoints()) {
            if (endpoint == null || !validText(endpoint.purpose(), 128)
                    || !validPath(endpoint.path()) || !paths.add(endpoint.path())) {
                throw invalid();
            }
        }

        String authMode = configuration.authMode();
        boolean jwt = "qweather_jwt".equals(authMode);
        if ((jwt && !"qweather".equals(id)) || ("qweather".equals(id) && configuration.enabled() && !jwt)) {
            throw invalid();
        }
        String defaultCity = configuration.defaultCity();
        if (defaultCity != null && !defaultCity.isBlank() && !validText(defaultCity, 64)) throw invalid();
        if (jwt && configuration.enabled() && (!validQWeatherUrl(url) || !validJwtId(configuration.jwtIssuer())
                || !validJwtId(configuration.jwtProject()) || !validJwtId(configuration.jwtKeyId()))) {
            throw invalid();
        }
        String authName = switch (authMode) {
            case "none" -> "";
            case "bearer", "qweather_jwt" -> "Authorization";
            default -> configuration.authName();
        };
        if (authName == null || (!authName.isEmpty() && !authName.matches("[A-Za-z0-9_-]{1,64}"))
                || (Set.of("query", "header").contains(authMode) && authName.isEmpty())) {
            throw invalid();
        }
        String secret = configuration.credential();
        boolean replacing = secret != null && !secret.isBlank();
        if (replacing && (configuration.clearCredential() || authMode.equals("none")
                || !(jwt ? QWeatherJwt.validPrivateKey(secret) : validText(secret, 4096)))) {
            throw invalid();
        }
        if ("qweather".equals(id)) SpeakerFunctionPlugins.lockActivationChanges(db);
        var existing = db.query("""
                SELECT auth_mode, auth_name, credential_cipher IS NOT NULL AS has_credential
                FROM speaker_third_party_service WHERE id=? AND version=?
                """, (row, index) -> new SavedAuth(row.getString(1), row.getString(2), row.getBoolean(3)),
                id, configuration.version());
        if (existing.isEmpty()) {
            throw new SpeakerFailure(409, "THIRD_PARTY_SERVICE_CHANGED");
        }
        SavedAuth saved = existing.getFirst();
        boolean sameAuth = authMode.equals(saved.mode()) && authName.equals(saved.name());
        boolean clear = configuration.clearCredential() || !sameAuth || authMode.equals("none");
        if (configuration.enabled() && (paths.isEmpty()
                || (!authMode.equals("none") && !replacing && (clear || !saved.hasCredential())))) {
            throw invalid();
        }
        String encrypted = replacing ? crypto.encryptThirdPartyCredential(id, secret.trim()) : null;
        if (db.update("""
                UPDATE speaker_third_party_service
                   SET display_name=?, base_url=?, endpoints=?, auth_mode=?, auth_name=?,
                       credential_cipher=CASE WHEN ? THEN ? ELSE credential_cipher END,
                       jwt_issuer=?, jwt_project=?, jwt_key_id=?, default_city=?,
                       enabled=?, version=version+1
                 WHERE id=? AND version=?
                """, configuration.name().trim(), url, writeEndpoints(configuration.endpoints()), authMode, authName,
                replacing || clear, encrypted, jwt ? configuration.jwtIssuer() : null,
                jwt ? configuration.jwtProject() : null, jwt ? configuration.jwtKeyId() : null,
                jwt && defaultCity != null && !defaultCity.isBlank() ? defaultCity.strip() : null,
                configuration.enabled(), id, configuration.version()) != 1) {
            throw new SpeakerFailure(409, "THIRD_PARTY_SERVICE_CHANGED");
        }
        if ("qweather".equals(id)) {
            if (!configuration.enabled()) {
                db.update("""
                        UPDATE speaker_function_plugin SET enabled=FALSE, version=version+1
                        WHERE plugin_id='SYSTEM_PLUGIN_WEATHER' AND enabled=TRUE
                        """);
            }
            db.update("UPDATE speaker_runtime SET revision=revision+1 WHERE id=1");
        }
    }

    private static boolean validQWeatherUrl(String value) {
        var uri = URI.create(value);
        return "https".equals(uri.getScheme()) && uri.getHost() != null
                && uri.getHost().endsWith(".qweatherapi.com") && uri.getPort() == -1
                && (uri.getPath().isEmpty() || "/".equals(uri.getPath()));
    }

    private static boolean validJwtId(String value) {
        return value != null && value.matches("[A-Za-z0-9_-]{1,128}");
    }

    private record SavedAuth(String mode, String name, boolean hasCredential) {}

    private static boolean validText(String text, int limit) {
        return text != null && !text.isBlank() && text.length() <= limit
                && text.chars().noneMatch(Character::isISOControl);
    }

    private static String normalizeUrl(String value, boolean enabled) {
        String url = value == null ? "" : value.trim();
        if (url.isEmpty() && !enabled) {
            return "";
        }
        try {
            URI parsed = URI.create(url);
            if (url.length() <= 1024 && parsed.getHost() != null
                    && ("https".equals(parsed.getScheme()) || "http".equals(parsed.getScheme()))
                    && parsed.getUserInfo() == null && parsed.getQuery() == null && parsed.getFragment() == null) {
                return url;
            }
        } catch (IllegalArgumentException ignored) {
            // Invalid URL.
        }
        throw invalid();
    }

    private static boolean validPath(String path) {
        if (!validText(path, 512) || !path.startsWith("/") || path.startsWith("//")) {
            return false;
        }
        try {
            URI uri = URI.create(path.replace("{latitude}", "latitude").replace("{longitude}", "longitude"));
            return uri.getQuery() == null && uri.getFragment() == null;
        } catch (IllegalArgumentException invalid) {
            return false;
        }
    }

    private List<Endpoint> readEndpoints(String value) {
        try {
            return json.readValue(value, ENDPOINTS);
        } catch (JsonProcessingException failure) {
            throw new IllegalStateException("Invalid stored third-party service endpoints", failure);
        }
    }

    private String writeEndpoints(List<Endpoint> endpoints) {
        try {
            return json.writeValueAsString(endpoints);
        } catch (JsonProcessingException failure) {
            throw invalid();
        }
    }

    private static SpeakerFailure invalid() {
        return new SpeakerFailure(400, "INVALID_THIRD_PARTY_SERVICE");
    }
}
