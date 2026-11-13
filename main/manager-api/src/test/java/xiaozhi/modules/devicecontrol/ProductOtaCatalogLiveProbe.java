package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.*;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.*;
import org.junit.jupiter.api.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.server.LocalServerPort;
import org.springframework.jdbc.core.JdbcTemplate;

/** Explicit opt-in network probe: -Dtest=ProductOtaCatalogLiveProbe. Never dispatches OTA. */
@SpringBootTest(webEnvironment=SpringBootTest.WebEnvironment.RANDOM_PORT,
        properties={"server.servlet.context-path=/xiaozhi"})
class ProductOtaCatalogLiveProbe {
    @LocalServerPort int port;
    @Autowired JdbcTemplate db;
    static final long ADMIN=960000000000099L;
    static final String TOKEN="ota96-isolated-live-probe";
    @BeforeEach void setup() {
        cleanup();
        db.update("INSERT INTO sys_user(id,username,super_admin,status) VALUES (?,'ota96-probe',1,1)",ADMIN);
        db.update("INSERT INTO sys_user_token(id,user_id,token,expire_date) VALUES (?,?,?,DATE_ADD(CURRENT_TIMESTAMP,INTERVAL 1 DAY))",ADMIN,ADMIN,TOKEN);
    }
    @AfterEach void cleanup() {
        db.update("DELETE FROM dc_ota_firmware WHERE operator_id=?",ADMIN);
        db.update("DELETE FROM sys_user_token WHERE user_id=?",ADMIN);
        db.update("DELETE FROM sys_user WHERE id=?",ADMIN);
    }
    @Test void importsExplicitPublicCandidateThroughRealHttpAndGithub() throws Exception {
        var client=HttpClient.newHttpClient();
        var request=HttpRequest.newBuilder(URI.create("http://localhost:"+port+"/xiaozhi/admin/product-ota/firmware"))
                .header("Authorization","Bearer "+TOKEN).header("Content-Type","application/json")
                .POST(HttpRequest.BodyPublishers.ofString("{\"release_id\":443720711,\"asset_id\":741863276}"));
        var response=client.send(request.build(),HttpResponse.BodyHandlers.ofString());
        assertEquals(200,response.statusCode(),response.body());
        var data=new ObjectMapper().readTree(response.body()).path("data");
        assertEquals("7cb0c36077d42d9b64f09727e176c83f6ca2f281abb04d988982dc82e30b8f8c",data.path("sha256").asText());
        assertEquals("f242b5b68cc3c565335c7eb0b048156209531ebd6fb5e9c54bdf809b9cc58d6e",data.path("elf_sha256").asText());
        assertEquals(4003072,data.path("image_size").asInt());
        assertEquals("2.2.6",data.path("app_version").asText());
        assertTrue(data.path("prerelease").asBoolean());
        System.out.println("OTA96 real GitHub candidate verified: "+data.toString());
    }
}
