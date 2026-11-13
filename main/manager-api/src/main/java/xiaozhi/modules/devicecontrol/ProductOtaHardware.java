package xiaozhi.modules.devicecontrol;

import java.util.*;
import com.fasterxml.jackson.databind.JsonNode;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;

/** The same firmware and delivered-hardware checks apply to both OTA transports. */
@Service
public class ProductOtaHardware {
    private final JdbcTemplate db;
    public ProductOtaHardware(JdbcTemplate db) { this.db=db; }
    Map<String,Object> firmware(String id) {
        var rows=db.queryForList("SELECT firmware_id,sha256,elf_sha256,app_version,board_type,flash_size,min_revision,max_revision,partition_layout,app_partition_bytes,image_size FROM dc_ota_firmware WHERE firmware_id=? AND verification_status='verified'",id);
        if(rows.size()!=1) throw new ControlFailure(404,"FIRMWARE_NOT_FOUND"); return rows.getFirst();
    }
    String compatibility(String device,Map<String,Object> image,JsonNode info) {
        var app=info.path("application"); var chip=info.path("chip_info");
        if(!app.path("elf_sha256").asText().matches("[a-f0-9]{64}") || app.path("version").asText().isEmpty()
                || !chip.path("revision").isIntegralNumber() || !info.path("flash_size").isIntegralNumber()
                || !info.path("board").path("type").isTextual()) return "hardware_unknown";
        var deliveries=db.queryForList("SELECT board_type,status,device_verified_at_ms,completed_at_ms FROM dc_delivery_operation WHERE device_id=?",device);
        if(deliveries.size()>1) return "incompatible_board";
        Map<String,Object> delivery=deliveries.isEmpty() ? Map.of() : deliveries.getFirst();
        boolean delivered="delivered".equals(delivery.get("status")) && delivery.get("device_verified_at_ms")!=null
                && delivery.get("completed_at_ms")!=null;
        if(!ProductOtaBoardTypes.matches((String)image.get("board_type"),info.path("board").path("type").asText(),
                (String)delivery.get("board_type"),delivered) || chip.path("model").asInt(-1)!=18) return "incompatible_board";
        if(chip.path("revision").asInt()<number(image,"min_revision") || chip.path("revision").asInt()>number(image,"max_revision")) return "incompatible_revision";
        if(info.path("flash_size").asLong()<number(image,"flash_size") || !"partitions/v2/16m.csv".equals(image.get("partition_layout"))) return "incompatible_layout";
        Object[][] layout={{"nvs",1,2,0x9000,0x4000},{"otadata",1,0,0xd000,0x2000},{"phy_init",1,1,0xf000,0x1000},
                {"ota_0",0,16,0x20000,0x3f0000},{"ota_1",0,17,0x410000,0x3f0000},{"assets",1,130,0x800000,0x800000}};
        var partitions=info.path("partition_table");
        if(!partitions.isArray() || partitions.size()!=layout.length || number(image,"image_size")>0x3f0000
                || number(image,"app_partition_bytes")!=0x3f0000 || !Set.of("ota_0","ota_1").contains(info.path("ota").path("label").asText())) return "incompatible_layout";
        for(var expected:layout) {
            int found=0;
            for(var partition:partitions) if(expected[0].equals(partition.path("label").asText())) {
                found++;
                String[] fields={"type","subtype","address","size"};
                for(int i=0;i<fields.length;i++) if(!partition.path(fields[i]).isIntegralNumber()
                        || partition.path(fields[i]).asLong()!=((Number)expected[i+1]).longValue()) return "incompatible_layout";
            }
            if(found!=1) return "incompatible_layout";
        }
        if(image.get("elf_sha256").equals(app.path("elf_sha256").asText())) return "already_running";
        return "eligible";
    }
    private long number(Map<String,Object> row,String key) { return ((Number)row.get(key)).longValue(); }
}
