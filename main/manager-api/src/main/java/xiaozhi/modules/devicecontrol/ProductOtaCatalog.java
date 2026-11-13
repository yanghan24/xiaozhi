package xiaozhi.modules.devicecontrol;

import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.*;
import java.nio.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.Semaphore;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;

/** Fixed project Release artifacts. Firmware bytes never appear in management responses. */
@Service
public class ProductOtaCatalog {
    private final JdbcTemplate db;
    private final GithubReleaseSource source;
    private final Semaphore importing = new Semaphore(1);
    private final ObjectMapper json = new ObjectMapper().enable(JsonParser.Feature.STRICT_DUPLICATE_DETECTION)
            .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS);
    private static final String FIELDS = "firmware_id,github_release_id,github_asset_id,release_tag,prerelease,source_commit,source_url,"
            + "manifest_sha256,sha256,elf_sha256,app_version,board_type,flash_size,min_revision,max_revision,partition_layout,"
            + "partition_table_sha256,app_partition_bytes,image_size,verification_status,operator_id,created_at_ms";
    public ProductOtaCatalog(JdbcTemplate db, GithubReleaseSource source) { this.db=db; this.source=source; }
    public void operator(long id) {
        if (db.queryForList("SELECT id FROM sys_user WHERE id=? AND super_admin=1 AND status=1",id).size()!=1)
            throw new ControlFailure(403,"OPERATOR_REQUIRED");
    }
    public List<Map<String,Object>> list() {
        return db.queryForList("SELECT " + FIELDS + " FROM dc_ota_firmware ORDER BY created_at_ms DESC,firmware_id");
    }
    public List<Map<String,Object>> releases(int page) {
        require(page>=1 && page<=100,"INVALID_PAGE");
        var result=new ArrayList<Map<String,Object>>();
        for(var release:source.releases(page)) {
            if(release.path("draft").asBoolean(true)) continue;
            var assets=new ArrayList<Map<String,Object>>();
            for(var asset:release.path("assets")) if(asset.path("name").asText().endsWith(".bin"))
                assets.add(Map.of("id",asset.path("id").asLong(),"name",asset.path("name").asText(),
                        "size",asset.path("size").asLong(),"digest",asset.path("digest").asText("")));
            result.add(Map.of("id",release.path("id").asLong(),"tag",release.path("tag_name").asText(),
                    "name",release.path("name").asText(""),"prerelease",release.path("prerelease").asBoolean(),"assets",assets));
        }
        return result;
    }
    public Map<String,Object> importFirmware(long operator,long releaseId,long assetId) {
        operator(operator);
        require(releaseId>0 && assetId>0,"INVALID_REQUEST");
        if(!importing.tryAcquire()) throw new ControlFailure(429,"IMPORT_IN_PROGRESS");
        try {
            var release=source.release(releaseId);
            require(release.path("id").asLong()==releaseId && !release.path("draft").asBoolean(true),"INVALID_RELEASE");
            var asset=find(release,assetId,null);
            var manifestAsset=find(release,0,"manifest.json");
            require(asset.path("name").asText().matches("[A-Za-z0-9_.-]+\\.bin"),"INVALID_APPLICATION_ASSET");
            byte[] manifestBytes=fetch(manifestAsset,65536);
            JsonNode manifest;
            try { manifest=json.readTree(manifestBytes); }
            catch(Exception bad) { throw new ControlFailure(400,"INVALID_MANIFEST"); }
            String tag=release.path("tag_name").asText();
            require(tag.length()<=255 && tag.equals(manifest.path("release_tag").asText())
                    && manifest.path("schema_version").asInt()==1,"INVALID_MANIFEST");
            var app=manifest.path("application");
            require(asset.path("name").asText().equals(app.path("file").asText()),"MANIFEST_ASSET_MISMATCH");
            byte[] image=fetch(asset,GithubReleaseSource.MAX_IMAGE);
            var inspected=OtaApplicationImage.inspect(image,app.path("sha256").asText());
            require(image.length==app.path("bytes").asLong() && inspected.version().equals(manifest.path("app_version").asText())
                    && inspected.elfSha256().equals(app.path("elf_sha256").asText()),"MANIFEST_IMAGE_MISMATCH");
            String board=manifest.path("board_type").asText();
            String commit=manifest.path("source_commit").asText();
            String layout=manifest.path("partition_layout").asText();
            String partitionHash=manifest.path("partition_table_sha256").asText();
            int capacity=manifest.path("app_partition_bytes").asInt();
            require(board.equals("waveshare-esp32-p4-wifi6-touch-lcd-4.3") && commit.matches("[a-f0-9]{40}")
                    && layout.equals("partitions/v2/16m.csv") && partitionHash.matches("[a-f0-9]{64}")
                    && capacity==4128768 && image.length<=capacity,"INCOMPATIBLE_LAYOUT");
            require(((image[3]&255)>>>4)==4,"FLASH_SIZE_MISMATCH");
            var header=ByteBuffer.wrap(image).order(ByteOrder.LITTLE_ENDIAN);
            int min=Math.max(Short.toUnsignedInt(header.getShort(15)),(image[14]&255)*100);
            int max=Short.toUnsignedInt(header.getShort(17)); if(max==0) max=65535;
            require(min<=max,"INVALID_REVISION_RANGE");
            var current=source.release(releaseId);
            require(tag.equals(current.path("tag_name").asText()) && !current.path("draft").asBoolean(true)
                    && release.path("prerelease").equals(current.path("prerelease"))
                    && sameAsset(asset,find(current,assetId,null)) && sameAsset(manifestAsset,find(current,0,"manifest.json")),"RELEASE_CHANGED");
            operator(operator);
            String id=UUID.randomUUID().toString().replace("-","");
            try {
                db.update("INSERT INTO dc_ota_firmware("+FIELDS+",image) VALUES ("+"?,".repeat(22)+"?)",
                        id,releaseId,assetId,tag,release.path("prerelease").asBoolean(),commit,
                        "https://api.github.com/repos/yanghan24/xiaozhi/releases/assets/"+assetId,
                        digest(manifestBytes),inspected.sha256(),inspected.elfSha256(),inspected.version(),board,
                        16777216,min,max,layout,partitionHash,capacity,image.length,"verified",operator,System.currentTimeMillis(),image);
            } catch(org.springframework.dao.DuplicateKeyException duplicate) { throw new ControlFailure(409,"FIRMWARE_ALREADY_IMPORTED"); }
            return db.queryForMap("SELECT "+FIELDS+" FROM dc_ota_firmware WHERE firmware_id=?",id);
        } finally { importing.release(); }
    }
    private JsonNode find(JsonNode release,long id,String name) {
        var matches=new ArrayList<JsonNode>();
        for(var asset:release.path("assets")) if(name==null ? asset.path("id").asLong()==id : name.equals(asset.path("name").asText())) matches.add(asset);
        require(matches.size()==1,"RELEASE_ASSET_MISSING"); return matches.getFirst();
    }
    private boolean sameAsset(JsonNode first,JsonNode second) {
        return List.of("id","name","size","digest","updated_at").stream().allMatch(field->first.path(field).equals(second.path(field)));
    }
    private byte[] fetch(JsonNode asset,int limit) {
        long size=asset.path("size").asLong(-1);
        require(size>0 && size<=limit,"ASSET_SIZE_INVALID");
        String expected=asset.path("digest").asText();
        require(expected.matches("sha256:[a-f0-9]{64}"),"ASSET_DIGEST_REQUIRED");
        byte[] bytes=source.asset(asset.path("id").asLong(),limit);
        require(bytes!=null && bytes.length==size && expected.equals("sha256:"+digest(bytes)),"ASSET_CHANGED_OR_INCOMPLETE");
        return bytes;
    }
    static String digest(byte[] bytes) {
        try { return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes)); }
        catch(Exception impossible) { throw new IllegalStateException(impossible); }
    }
    private static void require(boolean ok,String code) { if(!ok) throw new ControlFailure(400,code); }
}
