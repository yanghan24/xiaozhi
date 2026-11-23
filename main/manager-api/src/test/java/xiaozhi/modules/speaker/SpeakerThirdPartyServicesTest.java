package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Base64;
import java.util.List;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledIfEnvironmentVariable;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.core.io.ByteArrayResource;
import org.springframework.core.io.ClassPathResource;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.datasource.DriverManagerDataSource;
import org.springframework.jdbc.datasource.SingleConnectionDataSource;
import org.springframework.jdbc.datasource.init.ScriptUtils;

@EnabledIfEnvironmentVariable(named = "XIAOZHI_TEST_THIRD_PARTY_SERVICES", matches = "true")
class SpeakerThirdPartyServicesTest {
    @TempDir Path directory;

    @Test
    void migrationAndEditsKeepCredentialsPrivateAndRejectStaleVersions() throws Exception {
        var source = new DriverManagerDataSource(System.getenv("SPRING_DATASOURCE_DRUID_URL"),
                System.getenv("SPRING_DATASOURCE_DRUID_USERNAME"), System.getenv("SPRING_DATASOURCE_DRUID_PASSWORD"));
        try (var connection = source.getConnection()) {
            String sql;
            try (var input = new ClassPathResource("db/changelog/202611201600.sql").getInputStream()) {
                sql = new String(input.readAllBytes(), StandardCharsets.UTF_8);
            }
            ScriptUtils.executeSqlScript(connection, new ByteArrayResource(
                    sql.replace("CREATE TABLE", "CREATE TEMPORARY TABLE").getBytes(StandardCharsets.UTF_8)));
            try (var input = new ClassPathResource("db/changelog/202611261500.sql").getInputStream()) {
                sql = new String(input.readAllBytes(), StandardCharsets.UTF_8);
            }
            ScriptUtils.executeSqlScript(connection, new ByteArrayResource(
                    sql.replace("CREATE TABLE", "CREATE TEMPORARY TABLE").getBytes(StandardCharsets.UTF_8)));
            Path key = directory.resolve("key");
            Files.writeString(key, Base64.getEncoder().encodeToString(new byte[32]));
            Files.setPosixFilePermissions(key, PosixFilePermissions.fromString("rw-------"));
            var db = new JdbcTemplate(new SingleConnectionDataSource(connection, true));
            db.execute("CREATE TEMPORARY TABLE speaker_runtime(id INT PRIMARY KEY, revision BIGINT)");
            db.update("INSERT INTO speaker_runtime VALUES (1,0)");
            var settings = new SpeakerSettings(true, "https://fixture.example/oidc", "fixture", key.toString(), "");
            var json = new ObjectMapper();
            var crypto = new SpeakerCrypto(settings);
            var services = new SpeakerThirdPartyServices(db, json, crypto);

            assertEquals(5, services.list().size());
            assertTrue(services.list().stream().noneMatch(SpeakerThirdPartyServices.Entry::enabled));
            var endpoints = List.of(new SpeakerThirdPartyServices.Endpoint("/v3/ip", "定位"));
            services.save("amap", new SpeakerThirdPartyServices.Configuration("高德", "https://fixture.example",
                    endpoints, "query", "key", "test-credential", false, true, 0));
            var entry = services.list().stream().filter(row -> row.id().equals("amap")).findFirst().orElseThrow();
            assertTrue(entry.hasCredential() && entry.enabled());
            assertFalse(json.writeValueAsString(entry).contains("test-credential"));
            String encrypted = db.queryForObject("SELECT credential_cipher FROM speaker_third_party_service WHERE id='amap'", String.class);
            assertTrue(encrypted.startsWith("thirdparty1."));
            assertFalse(encrypted.contains("test-credential"));
            assertTrue(services.amapConfiguration().isEmpty());

            services.save("amap", new SpeakerThirdPartyServices.Configuration("高德 II", entry.url(), endpoints,
                    "query", "key", null, false, true, 1));
            assertEquals(encrypted, db.queryForObject("SELECT credential_cipher FROM speaker_third_party_service WHERE id='amap'", String.class));
            assertThrows(SpeakerFailure.class, () -> services.save("amap",
                    new SpeakerThirdPartyServices.Configuration("旧编辑", entry.url(), endpoints, "query", "key", null, false, true, 1)));
            assertThrows(SpeakerFailure.class, () -> services.save("amap",
                    new SpeakerThirdPartyServices.Configuration("误填地址", entry.url() + "?key=fixture", endpoints, "query", "key", null, false, true, 2)));
            services.save("amap", new SpeakerThirdPartyServices.Configuration("高德 II", entry.url(), endpoints,
                    "query", "key", null, true, false, 2));
            assertNull(db.queryForObject("SELECT credential_cipher FROM speaker_third_party_service WHERE id='amap'", String.class));

            var amapEndpoints = List.of(new SpeakerThirdPartyServices.Endpoint("/v3/ip", "定位"),
                    new SpeakerThirdPartyServices.Endpoint("/v3/weather/weatherInfo", "天气"));
            services.save("amap", new SpeakerThirdPartyServices.Configuration("高德", "https://restapi.amap.com",
                    amapEndpoints, "query", "key", "fixture-amap", false, true, 3));
            var configuration = services.amapConfiguration().orElseThrow();
            assertEquals(4, configuration.version());
            assertEquals("fixture-amap", configuration.credential());
            assertFalse(configuration.toString().contains(configuration.credential()));

            var qweather = services.list().stream().filter(row -> row.id().equals("qweather")).findFirst().orElseThrow();
            var keyPair = java.security.KeyPairGenerator.getInstance("Ed25519").generateKeyPair();
            var pem = Base64.getEncoder().encodeToString(keyPair.getPrivate().getEncoded());
            var jwt = new SpeakerThirdPartyServices.Configuration(qweather.name(), "https://fixture.qweatherapi.com",
                    qweather.endpoints(), "qweather_jwt", "", pem, false, true, qweather.version(),
                    "developer", "project", "credential", "长沙");
            services.save("qweather", jwt);
            assertTrue(services.qweatherReady());
            assertEquals(pem, services.qweatherConfiguration().orElseThrow().privateKey());
            assertFalse(json.writeValueAsString(services.list()).contains(pem));
            assertEquals(1L, db.queryForObject("SELECT revision FROM speaker_runtime WHERE id=1", Long.class));

            var plugins = new SpeakerFunctionPlugins(db, json, crypto, services);
            db.execute("CREATE TEMPORARY TABLE ai_model_provider(id VARCHAR(32), name VARCHAR(64), provider_code VARCHAR(64), "
                    + "model_type VARCHAR(16), sort INT, fields JSON)");
            db.execute("CREATE TEMPORARY TABLE ai_agent_plugin_mapping(agent_id VARCHAR(32), plugin_id VARCHAR(32), param_info JSON)");
            db.update("INSERT INTO ai_model_provider VALUES ('search', '联网搜索', 'web_search', 'Plugin', 1, ?)",
                    """
                    [{"key":"provider","type":"string","label":"搜索源","default":"metaso"},
                     {"key":"max_results","type":"string","label":"返回数量","default":"5"},
                     {"key":"api_key","type":"string","label":"凭据","default":"mk-XXXX","selected":false}]
                    """);
            var unavailable = assertThrows(SpeakerFailure.class,
                    () -> plugins.setEnabled("search", new SpeakerFunctionPlugins.Activation(true, 0)));
            assertEquals("PLUGIN_CONFIGURATION_REQUIRED", unavailable.getMessage());
            assertFalse(plugins.list().getFirst().configured());
            assertTrue(plugins.choices().isEmpty());
            String legacyKey = "legacy-fixture-key";
            String legacyConfig = "{\"api_key\":\"" + legacyKey + "\"}";
            db.update("INSERT INTO ai_agent_plugin_mapping VALUES ('agent1', 'search', ?)", legacyConfig);
            var existing = new xiaozhi.modules.agent.entity.AgentPluginMapping();
            existing.setPluginId("search");
            existing.setProviderCode("web_search");
            existing.setParamInfo(legacyConfig);
            assertTrue(plugins.list().getFirst().configured());
            assertFalse(plugins.list().getFirst().enabled());
            assertTrue(plugins.choices().isEmpty());
            assertFalse(json.writeValueAsString(plugins.list()).contains(legacyKey));
            db.update("INSERT INTO ai_agent_plugin_mapping VALUES ('agent2', 'search', '{\"api_key\":\"other-fixture-key\"}')");
            assertTrue(plugins.list().getFirst().legacyConflict());
            assertTrue(plugins.choices().isEmpty());
            assertTrue(plugins.runtime(List.of(existing)).isEmpty());

            var pluginParams = java.util.Map.<String, Object>of("provider", "metaso", "max_results", "5");
            plugins.save("search", new SpeakerFunctionPlugins.Input(null, 0, pluginParams, "global-fixture-key", false));
            assertTrue(plugins.list().getFirst().configured());
            assertTrue(plugins.choices().isEmpty());
            plugins.setEnabled("search", new SpeakerFunctionPlugins.Activation(true, 1));
            assertTrue(plugins.choices().getFirst().ready());
            assertFalse(json.writeValueAsString(plugins.list()).contains("global-fixture-key"));
            assertTrue(plugins.runtime(List.of(existing)).get("web_search").toString().contains("global-fixture-key"));
            assertThrows(SpeakerFailure.class, () -> plugins.save("search", new SpeakerFunctionPlugins.Input(true, 2,
                    java.util.Map.of("provider", "tavily", "max_results", "5"), null, false)));
            plugins.setEnabled("search", new SpeakerFunctionPlugins.Activation(false, 2));
            assertTrue(plugins.list().getFirst().configured());
            assertTrue(plugins.choices().isEmpty());
            assertTrue(plugins.runtime(List.of(existing)).isEmpty());
            assertThrows(SpeakerFailure.class, () -> plugins.validateSelections(List.of("search"), java.util.Set.of()));
            plugins.validateSelections(List.of("search"), java.util.Set.of("search"));
            plugins.setEnabled("search", new SpeakerFunctionPlugins.Activation(true, 3));
            plugins.save("search", new SpeakerFunctionPlugins.Input(null, 4, pluginParams, null, true));
            assertFalse(plugins.list().getFirst().configured());
            assertFalse(plugins.list().getFirst().enabled());
            assertTrue(plugins.choices().isEmpty());

            db.update("INSERT INTO ai_model_provider VALUES ('SYSTEM_PLUGIN_WEATHER', '天气查询', 'get_weather', 'Plugin', 2, '[]')");
            db.update("INSERT INTO ai_agent_plugin_mapping VALUES ('agent1', 'SYSTEM_PLUGIN_WEATHER', '{}')");
            assertFalse(plugins.weatherSelected("agent1"));
            plugins.setEnabled("SYSTEM_PLUGIN_WEATHER", new SpeakerFunctionPlugins.Activation(true, 0));
            assertTrue(plugins.weatherSelected("agent1"));
            services.save("qweather", new SpeakerThirdPartyServices.Configuration(qweather.name(),
                    "https://fixture.qweatherapi.com", qweather.endpoints(), "qweather_jwt", "", null, false, false,
                    qweather.version() + 1, "developer", "project", "credential", "长沙"));
            assertFalse(plugins.weatherSelected("agent1"));
            assertTrue(plugins.choices().isEmpty());
            services.save("qweather", new SpeakerThirdPartyServices.Configuration(qweather.name(),
                    "https://fixture.qweatherapi.com", qweather.endpoints(), "qweather_jwt", "", null, false, true,
                    qweather.version() + 2, "developer", "project", "credential", "长沙"));
            assertTrue(services.qweatherReady());
            assertFalse(plugins.weatherSelected("agent1"));
            assertTrue(plugins.choices().isEmpty());
        }
    }
}
