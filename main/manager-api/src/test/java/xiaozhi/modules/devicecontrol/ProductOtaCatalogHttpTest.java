package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.when;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.*;
import org.junit.jupiter.api.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.server.LocalServerPort;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.boot.test.mock.mockito.MockBean;
import java.util.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.io.ByteArrayOutputStream;
import java.security.MessageDigest;

/** Exercises the actual HTTP authentication chain and persisted catalog on Ubuntu. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT,
        properties = {"server.servlet.context-path=/xiaozhi", "server.forward-headers-strategy=native"})
class ProductOtaCatalogHttpTest {
    static final long ADMIN = 960000000000001L;
    static final String TOKEN = "ota96-admin-fixture";
    static final ObjectMapper JSON = new ObjectMapper();
    @LocalServerPort int port;
    @Autowired JdbcTemplate db;
    @MockBean GithubReleaseSource source;
    final HttpClient http = HttpClient.newHttpClient();
    @BeforeEach void setup() {
        cleanup();
        db.update("INSERT INTO sys_user(id,username,super_admin,status) VALUES (?, 'ota96-admin',1,1)", ADMIN);
        db.update("INSERT INTO sys_user_token(id,user_id,token,expire_date) VALUES (?,?,?,DATE_ADD(CURRENT_TIMESTAMP,INTERVAL 1 DAY))", ADMIN, ADMIN, TOKEN);
    }
    @AfterEach void cleanup() {
        db.update("DELETE FROM dc_ota_firmware WHERE operator_id=?",ADMIN);
        db.update("DELETE FROM sys_user_token WHERE user_id=?", ADMIN);
        db.update("DELETE FROM sys_user WHERE id=?", ADMIN);
    }
    HttpResponse<String> get(String path, String token) throws Exception {
        var request = HttpRequest.newBuilder(URI.create("http://localhost:" + port + "/xiaozhi/admin/product-ota/" + path));
        if (token != null) request.header("Authorization", "Bearer " + token);
        request.header("X-Forwarded-Proto", "https");
        return http.send(request.GET().build(), HttpResponse.BodyHandlers.ofString());
    }
    @Test void administratorCanReadFixedCatalogButOtherIdentitiesCannot() throws Exception {
        var response = get("firmware", TOKEN);
        assertEquals(200, response.statusCode(), response.body());
        assertTrue(JSON.readTree(response.body()).path("data").isArray(), response.body());
        assertEquals(401, get("firmware", null).statusCode());
        assertEquals(401, get("firmware", "forged").statusCode());
        db.update("UPDATE sys_user SET status=0 WHERE id=?", ADMIN);
        assertEquals(401, get("firmware", TOKEN).statusCode());
    }
    HttpResponse<String> importAsset(long release, long asset) throws Exception {
        return http.send(HttpRequest.newBuilder(URI.create("http://localhost:" + port + "/xiaozhi/admin/product-ota/firmware"))
                .header("Authorization", "Bearer " + TOKEN).header("X-Forwarded-Proto", "https")
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString("{\"release_id\":" + release + ",\"asset_id\":" + asset + "}"))
                .build(), HttpResponse.BodyHandlers.ofString());
    }
    String fixture(long id, int build) throws Exception {
        byte[] image = new byte[304];
        var header = ByteBuffer.wrap(image).order(ByteOrder.LITTLE_ENDIAN);
        image[0]=(byte)0xe9; image[1]=1; image[3]=0x40;
        header.putShort(12, (short)0x12); header.putShort(15, (short)100);
        header.putShort(17, (short)399); header.putInt(28,256); header.putInt(32,0xabcd5432);
        System.arraycopy("2.2.6".getBytes(),0,image,48,5);
        Arrays.fill(image,176,208,(byte)build);
        int checksum=0xef; for(int p=32;p<288;p++) checksum ^= image[p]&255;
        image[303]=(byte)checksum;
        String sha=HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(image));
        String elf=HexFormat.of().formatHex(Arrays.copyOfRange(image,176,208));
        var manifest=JSON.valueToTree(Map.of("schema_version",1,"release_tag","firmware/test-"+id,
                "source_commit","a".repeat(40),"board_type","waveshare-esp32-p4-wifi6-touch-lcd-4.3",
                "app_version","2.2.6","partition_layout","partitions/v2/16m.csv","app_partition_bytes",4128768,
                "partition_table_sha256","b".repeat(64),"application",Map.of("file","app.bin","bytes",304,"sha256",sha,"elf_sha256",elf)));
        byte[] metadata=JSON.writeValueAsBytes(manifest);
        var release=JSON.valueToTree(Map.of("id",id,"tag_name","firmware/test-"+id,"name","Candidate", "draft",false,"prerelease",true,
                "assets",List.of(Map.of("id",id*10,"name","app.bin","size",304,"digest","sha256:"+sha),
                        Map.of("id",id*10+1,"name","manifest.json","size",metadata.length,"digest","sha256:"+HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(metadata))))));
        when(source.release(id)).thenReturn(release);
        when(source.releases(1)).thenReturn(JSON.valueToTree(List.of(release)));
        when(source.asset(id*10, GithubReleaseSource.MAX_IMAGE)).thenReturn(image);
        when(source.asset(id*10+1, 65536)).thenReturn(metadata);
        return sha;
    }
    HttpResponse<String> upload(String board) throws Exception {
        fixture(9601,1);
        byte[] image=source.asset(96010,GithubReleaseSource.MAX_IMAGE);
        var manifest=(com.fasterxml.jackson.databind.node.ObjectNode)JSON.readTree(source.asset(96011,65536));
        manifest.put("board_type",board);
        String boundary="ota96-multipart-boundary";
        var body=new ByteArrayOutputStream();
        body.write(("--"+boundary+"\r\nContent-Disposition: form-data; name=\"image\"; filename=\"app.bin\"\r\nContent-Type: application/octet-stream\r\n\r\n").getBytes(StandardCharsets.UTF_8));
        body.write(image);
        body.write(("\r\n--"+boundary+"\r\nContent-Disposition: form-data; name=\"manifest\"; filename=\"manifest.json\"\r\nContent-Type: application/json\r\n\r\n").getBytes(StandardCharsets.UTF_8));
        body.write(JSON.writeValueAsBytes(manifest));
        body.write(("\r\n--"+boundary+"--\r\n").getBytes(StandardCharsets.UTF_8));
        return http.send(HttpRequest.newBuilder(URI.create("http://localhost:"+port+"/xiaozhi/admin/product-ota/firmware/upload"))
                .header("Authorization","Bearer "+TOKEN).header("Content-Type","multipart/form-data; boundary="+boundary)
                .POST(HttpRequest.BodyPublishers.ofByteArray(body.toByteArray())).build(),HttpResponse.BodyHandlers.ofString());
    }
    @Test void backendUploadPreservesThePreciseBoardVariant() throws Exception {
        String board="waveshare-esp32-p4-wifi6-touch-lcd-4.3";
        var response=upload(board);
        assertEquals(200,response.statusCode(),response.body());
        var uploaded=JSON.readTree(response.body()).path("data");
        assertEquals(board,uploaded.path("board_type").asText());
        assertEquals("backend_upload",uploaded.path("source_kind").asText());
        assertEquals("verified",uploaded.path("verification_status").asText());
        assertEquals(board,db.queryForObject("SELECT board_type FROM dc_ota_firmware WHERE firmware_id=?",String.class,uploaded.path("firmware_id").asText()));
        assertEquals(board,JSON.readTree(get("firmware",TOKEN).body()).path("data").path(0).path("board_type").asText());
    }
    @Test void backendUploadRejectsFamilyAndUnknownNamesWithoutSavingThem() throws Exception {
        for(String board:List.of("esp32-p4-wifi6-touch-lcd","waveshare-esp32-p4-wifi6-touch-lcd-7","unknown-board")) {
            var response=upload(board);
            assertEquals(400,response.statusCode(),response.body());
            assertEquals("INCOMPATIBLE_LAYOUT",JSON.readTree(response.body()).path("code").asText());
        }
        assertEquals(0,JSON.readTree(get("firmware",TOKEN).body()).path("data").size());
    }
    @Test void importsFixedReleaseAndReadsDistinctBuildsWithSameVersion() throws Exception {
        String sha=fixture(9601,1);
        var releases=get("github-releases?page=1",TOKEN);
        assertEquals(200,releases.statusCode(),releases.body());
        assertTrue(JSON.readTree(releases.body()).path("data").path(0).path("prerelease").asBoolean(),releases.body());
        var first=importAsset(9601,96010);
        assertEquals(200,first.statusCode(),first.body());
        String other=fixture(9602,2);
        assertEquals(200,importAsset(9602,96020).statusCode());
        var catalog=JSON.readTree(get("firmware",TOKEN).body()).path("data");
        assertTrue(catalog.toString().contains(sha)); assertTrue(catalog.toString().contains(other));
        var value=JSON.readTree(first.body()).path("data");
        assertEquals("2.2.6",value.path("app_version").asText());
        assertEquals("verified",value.path("verification_status").asText());
        assertEquals("waveshare-esp32-p4-wifi6-touch-lcd-4.3",value.path("board_type").asText());
        assertEquals(100,value.path("min_revision").asInt());
        assertEquals(4128768,value.path("app_partition_bytes").asInt());
        assertFalse(catalog.toString().contains("image\""));
    }
    @Test void rejectsMalformedImportWithoutAcceptingAnArbitraryUrl() throws Exception {
        fixture(9601,1);
        var response=http.send(HttpRequest.newBuilder(URI.create("http://localhost:"+port+"/xiaozhi/admin/product-ota/firmware"))
                .header("Authorization","Bearer "+TOKEN).header("X-Forwarded-Proto","https").header("Content-Type","application/json")
                .POST(HttpRequest.BodyPublishers.ofString("{\"release_id\":9601,\"asset_id\":96010,\"url\":\"http://127.0.0.1/private\"}"))
                .build(),HttpResponse.BodyHandlers.ofString());
        assertEquals(400,response.statusCode(),response.body());
        assertEquals(0,JSON.readTree(get("firmware",TOKEN).body()).path("data").size());
    }
    @Test void rejectsChangedTruncatedOversizedAndNonApplicationArtifactsWithoutCatalogEntries() throws Exception {
        fixture(9601,1);
        byte[] original=source.asset(96010,GithubReleaseSource.MAX_IMAGE);
        when(source.asset(96010,GithubReleaseSource.MAX_IMAGE)).thenReturn(Arrays.copyOf(original,200));
        assertEquals(400,importAsset(9601,96010).statusCode());
        fixture(9601,1);
        var release=(com.fasterxml.jackson.databind.node.ObjectNode)source.release(9601).deepCopy();
        ((com.fasterxml.jackson.databind.node.ObjectNode)release.path("assets").get(0)).put("size",9000000);
        when(source.release(9601)).thenReturn(release);
        assertEquals(400,importAsset(9601,96010).statusCode());
        fixture(9601,1);
        original[0]=0; // A matching published digest still cannot turn arbitrary bytes into an application.
        replaceImage(original);
        var bad=importAsset(9601,96010);
        assertEquals(400,bad.statusCode(),bad.body());
        assertEquals("INVALID_P4_APPLICATION_IMAGE",JSON.readTree(bad.body()).path("code").asText());
        assertEquals(0,JSON.readTree(get("firmware",TOKEN).body()).path("data").size());
    }
    void replaceImage(byte[] image) throws Exception {
        String digest="sha256:"+HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(image));
        var release=(com.fasterxml.jackson.databind.node.ObjectNode)source.release(9601).deepCopy();
        ((com.fasterxml.jackson.databind.node.ObjectNode)release.path("assets").get(0)).put("digest",digest);
        when(source.release(9601)).thenReturn(release);
        when(source.asset(96010,GithubReleaseSource.MAX_IMAGE)).thenReturn(image);
    }
    @Test void rejectsReleaseMutationBetweenFetchAndCommit() throws Exception {
        fixture(9601,1);
        var original=source.release(9601);
        var changed=(com.fasterxml.jackson.databind.node.ObjectNode)original.deepCopy();
        changed.put("tag_name","replacement");
        when(source.release(9601)).thenReturn(original,changed);
        var response=importAsset(9601,96010);
        assertEquals(400,response.statusCode(),response.body());
        assertEquals("RELEASE_CHANGED",JSON.readTree(response.body()).path("code").asText());
        assertEquals(0,JSON.readTree(get("firmware",TOKEN).body()).path("data").size());
    }
    @Test void ordinaryUserAndNonAdminCredentialsCannotImportOrRead() throws Exception {
        fixture(9601,1);
        db.update("UPDATE sys_user SET super_admin=0 WHERE id=?",ADMIN);
        assertEquals(403,importAsset(9601,96010).statusCode());
        assertEquals(403,get("github-releases",TOKEN).statusCode());
        for(String token:List.of("customer-session-fixture","device-fixture","delivery-tool-fixture"))
            assertEquals(401,get("firmware",token).statusCode());
    }
    @Test void supportsExistingPrivateManagementTransport() throws Exception {
        var response=http.send(HttpRequest.newBuilder(URI.create("http://localhost:"+port+"/xiaozhi/admin/product-ota/firmware"))
                .header("Authorization","Bearer "+TOKEN).GET().build(),HttpResponse.BodyHandlers.ofString());
        assertEquals(200,response.statusCode(),response.body());
    }
}
