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

@EnabledIfEnvironmentVariable(named = "SPRING_DATASOURCE_DRUID_URL", matches = "jdbc:mysql://172\\.16\\.18\\.104:13306/xiaozhi_dev\\?.*")
class SpeakerThirdPartyServicesTest {
    @TempDir Path directory;

    @Test
    void migrationAndEditsKeepCredentialsPrivateAndRejectStaleVersions() throws Exception {
        var source = new DriverManagerDataSource(System.getenv("SPRING_DATASOURCE_DRUID_URL"),
                "xiaozhi_dev", System.getenv("SPRING_DATASOURCE_DRUID_PASSWORD"));
        try (var connection = source.getConnection()) {
            String sql;
            try (var input = new ClassPathResource("db/changelog/202611201600.sql").getInputStream()) {
                sql = new String(input.readAllBytes(), StandardCharsets.UTF_8);
            }
            ScriptUtils.executeSqlScript(connection, new ByteArrayResource(
                    sql.replace("CREATE TABLE", "CREATE TEMPORARY TABLE").getBytes(StandardCharsets.UTF_8)));
            Path key = directory.resolve("key");
            Files.writeString(key, Base64.getEncoder().encodeToString(new byte[32]));
            Files.setPosixFilePermissions(key, PosixFilePermissions.fromString("rw-------"));
            var db = new JdbcTemplate(new SingleConnectionDataSource(connection, true));
            var settings = new SpeakerSettings(true, "https://fixture.example/oidc", "fixture", key.toString(), "");
            var json = new ObjectMapper();
            var services = new SpeakerThirdPartyServices(db, json, new SpeakerCrypto(settings));

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
        }
    }
}
