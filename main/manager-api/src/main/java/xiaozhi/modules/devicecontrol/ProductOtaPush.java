package xiaozhi.modules.devicecontrol;

import java.util.*;
import java.util.concurrent.*;
import jakarta.annotation.PreDestroy;
import com.fasterxml.jackson.databind.*;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;

@Service
public class ProductOtaPush {
    private final JdbcTemplate db;
    private final ProductOtaBridge bridge;
    private final ObjectMapper json=new ObjectMapper();
    private final ProductOtaDownloads downloads;
    private final ProductOtaCatalog catalog;
    private final TransactionTemplate transactions;
    private final ExecutorService statusWorkers=Executors.newFixedThreadPool(8);
    @PreDestroy public void close() { statusWorkers.shutdownNow(); }
    private static final String PUBLIC_FIELDS="operation_id,preview_id,request_key,device_id,firmware_id,artifact_sha256,target_version,target_elf_sha256,status,reason,observation_status,observed_at_ms,created_at_ms,updated_at_ms";
    public ProductOtaPush(JdbcTemplate db, ProductOtaBridge bridge,ProductOtaDownloads downloads,
            ProductOtaCatalog catalog,PlatformTransactionManager tx) {
        this.db=db; this.bridge=bridge; this.downloads=downloads; this.catalog=catalog; this.transactions=new TransactionTemplate(tx);
    }
    public Map<String,Object> preview(long operator,String device,String firmware) {
        validateDevice(device); validateId(firmware);
        if(db.queryForList("SELECT device_id FROM dc_device WHERE device_id=?",device).size()!=1)
            throw new ControlFailure(404,"DEVICE_NOT_FOUND");
        var image=firmware(firmware);
        var result=new LinkedHashMap<String,Object>();
        result.put("device_id",device); result.put("firmware_id",firmware);
        result.put("target_version",image.get("app_version")); result.put("target_elf_sha256",image.get("elf_sha256"));
        result.put("eligible",false); result.put("reason","bridge_unavailable");
        result.put("current_version",""); result.put("current_elf_sha256","");
        result.put("preview_id",""); result.put("expires_at_ms",0);
        if(pending(device)) { result.put("reason","operation_pending"); return result; }
        try {
            var session=bridge.call(Map.of("action","status","device_id",device));
            if(!session.path("status").asText().equals("current")) { result.put("reason",session.path("status").asText()); return result; }
            var request=sessionRequest("inspect",device,session);
            var inspected=bridge.call(request);
            if(!inspected.path("status").asText().equals("ready")) { result.put("reason",inspected.path("status").asText()); return result; }
            var info=inspected.path("system_info");
            result.put("current_version",info.path("application").path("version").asText());
            result.put("current_elf_sha256",info.path("application").path("elf_sha256").asText());
            String reason=compatibility(image,info);
            result.put("reason",reason);
            if(!reason.equals("eligible")) return result;
            String id=id(); long expires=System.currentTimeMillis()+120000;
            var snapshot=new LinkedHashMap<String,Object>(request);
            snapshot.put("sha256",image.get("sha256")); snapshot.put("current_elf_sha256",result.get("current_elf_sha256"));
            db.update("INSERT INTO dc_ota_preview VALUES (?,?,?,?,?,?)",id,operator,device,firmware,encode(snapshot),expires);
            result.put("eligible",true); result.put("preview_id",id); result.put("expires_at_ms",expires);
        } catch(ControlFailure unavailable) { result.put("reason","bridge_unavailable"); }
        return result;
    }
    private boolean pending(String device) {
        return !db.queryForList("SELECT operation_id FROM dc_ota_operation WHERE active_device=?",device).isEmpty();
    }
    public List<Map<String,Object>> operations() {
        return db.queryForList("SELECT "+PUBLIC_FIELDS+" FROM dc_ota_operation ORDER BY created_at_ms DESC,operation_id");
    }
    public Map<String,Object> operation(String operation) {
        validateId(operation);
        var rows=db.queryForList("SELECT "+PUBLIC_FIELDS+" FROM dc_ota_operation WHERE operation_id=?",operation);
        if(rows.size()!=1) throw new ControlFailure(404,"OPERATION_NOT_FOUND");
        return rows.getFirst();
    }
    /** Observe authenticated running code separately; this path can never dispatch. */
    public Map<String,Object> observe(String operation) {
        var visible=operation(operation);
        if("not_sent".equals(visible.get("status")) || "target_running".equals(visible.get("observation_status"))) return visible;
        try {
            String device=(String)visible.get("device_id");
            var session=bridge.call(Map.of("action","status","device_id",device));
            if(!session.path("status").asText().equals("current")) return visible;
            var row=db.queryForMap("SELECT snapshot FROM dc_ota_operation WHERE operation_id=?",operation);
            var original=json.readTree((String)row.get("snapshot"));
            if(original.path("session_id").equals(session.path("session_id"))) return visible;
            var request=sessionRequest("reconcile",device,session); request.put("operation_id",operation);
            var result=bridge.call(request);
            if(result.path("observation_status").asText().equals("target_running") && result.path("observed_at_ms").isIntegralNumber()) {
                long at=result.path("observed_at_ms").asLong();
                if(at>=number(visible,"created_at_ms") && at<=System.currentTimeMillis()+5000)
                    db.update("UPDATE dc_ota_operation SET observation_status='target_running',observed_at_ms=?,active_device=NULL,updated_at_ms=? WHERE operation_id=?",
                            at,System.currentTimeMillis(),operation);
            }
        } catch(Exception unavailable) { /* Keep the durable original result when observation is unavailable. */ }
        return operation(operation);
    }
    private record Reserved(String id,boolean fresh) { }
    public Map<String,Object> confirm(long operator,String preview,String requestKey) {
        validateId(preview); validateId(requestKey);
        var prior=db.queryForList("SELECT operation_id,preview_id FROM dc_ota_operation WHERE operator_id=? AND request_key=?",operator,requestKey);
        if(!prior.isEmpty()) {
            if(!preview.equals(prior.getFirst().get("preview_id"))) throw new ControlFailure(409,"REQUEST_CONFLICT");
            return operation((String)prior.getFirst().get("operation_id"));
        }
        var previews=db.queryForList("SELECT * FROM dc_ota_preview WHERE preview_id=? AND operator_id=?",preview,operator);
        if(previews.size()!=1) throw new ControlFailure(404,"PREVIEW_NOT_FOUND");
        var chosen=previews.getFirst(); String device=(String)chosen.get("device_id");
        var image=firmware((String)chosen.get("firmware_id"));
        // Validate configuration before reservation; a missing download service cannot create a live command.
        downloads.signature("0".repeat(32),(String)image.get("sha256"),System.currentTimeMillis()/1000+600);
        Reserved reservation=transactions.execute(tx->{
            db.queryForList("SELECT device_id FROM dc_device WHERE device_id=? FOR UPDATE",device);
            var repeated=db.queryForList("SELECT operation_id,preview_id FROM dc_ota_operation WHERE operator_id=? AND request_key=?",operator,requestKey);
            if(!repeated.isEmpty()) {
                if(!preview.equals(repeated.getFirst().get("preview_id"))) throw new ControlFailure(409,"REQUEST_CONFLICT");
                return new Reserved((String)repeated.getFirst().get("operation_id"),false);
            }
            var already=db.queryForList("SELECT operation_id FROM dc_ota_operation WHERE preview_id=?",preview);
            if(!already.isEmpty()) return new Reserved((String)already.getFirst().get("operation_id"),false);
            if(pending(device)) throw new ControlFailure(409,"DEVICE_OPERATION_PENDING");
            if(number(chosen,"expires_at_ms")<=System.currentTimeMillis()) throw new ControlFailure(409,"PREVIEW_EXPIRED");
            String operation=id(); long now=System.currentTimeMillis();
            db.update("INSERT INTO dc_ota_operation(operation_id,preview_id,request_key,operator_id,device_id,active_device,firmware_id,artifact_sha256,target_version,target_elf_sha256,snapshot,status,reason,created_at_ms,updated_at_ms,download_expires) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    operation,preview,requestKey,operator,device,device,chosen.get("firmware_id"),image.get("sha256"),image.get("app_version"),image.get("elf_sha256"),chosen.get("snapshot"),"dispatch_reserved","awaiting_result",now,now,now/1000+600);
            return new Reserved(operation,true);
        });
        if(!reservation.fresh()) return operation(reservation.id());
        String status="result_unknown",reason="bridge_response_unavailable";
        boolean dispatchAttempted=false;
        try {
            catalog.operator(operator);
            var snapshot=json.readTree((String)chosen.get("snapshot"));
            if(!snapshot.path("sha256").asText().equals(image.get("sha256"))) {
                status="not_sent"; reason="artifact_changed";
            } else {
                var inspect=sessionRequest("inspect",device,snapshot);
                var checked=bridge.call(inspect);
                String compatible=checked.path("status").asText().equals("ready") ? compatibility(image,checked.path("system_info")) : checked.path("status").asText();
                if(!compatible.equals("eligible") || !snapshot.path("current_elf_sha256").asText().equals(checked.path("system_info").path("application").path("elf_sha256").asText())) {
                    status="not_sent"; reason=compatible.equals("eligible") ? "session_changed" : compatible;
                } else {
                    var record=db.queryForMap("SELECT download_expires FROM dc_ota_operation WHERE operation_id=?",reservation.id());
                    var dispatch=sessionRequest("dispatch",device,snapshot);
                    dispatch.put("operation_id",reservation.id()); dispatch.put("artifact_sha256",image.get("sha256"));
                    dispatch.put("target_elf_sha256",image.get("elf_sha256")); dispatch.put("target_version",image.get("app_version"));
                    dispatch.put("url",downloads.url(reservation.id(),(String)image.get("sha256"),number(record,"download_expires")));
                    dispatchAttempted=true;
                    var result=bridge.call(dispatch); String returned=result.path("status").asText();
                    if(Set.of("accepted_unconfirmed","result_unknown","result_requires_verification","not_sent").contains(returned)) {
                        status=returned; reason=result.path("reason").asText(returned);
                    }
                }
            }
        } catch(Exception unavailable) {
            // Once persisted, an interrupted request is never dispatched again, including after restart.
            if(!dispatchAttempted) { status="not_sent"; reason="preflight_unavailable"; }
        }
        db.update("UPDATE dc_ota_operation SET status=?,reason=?,active_device=CASE WHEN observation_status='target_running' THEN NULL ELSE ? END,updated_at_ms=? WHERE operation_id=?",
                status,safeReason(reason),status.equals("not_sent") ? null : device,System.currentTimeMillis(),reservation.id());
        return operation(reservation.id());
    }
    private String safeReason(String reason) { return reason.matches("[a-z_]{1,64}") ? reason : "result_requires_verification"; }
    private Map<String,Object> firmware(String id) {
        var rows=db.queryForList("SELECT firmware_id,sha256,elf_sha256,app_version,board_type,flash_size,min_revision,max_revision,partition_layout,app_partition_bytes,image_size FROM dc_ota_firmware WHERE firmware_id=? AND verification_status='verified'",id);
        if(rows.size()!=1) throw new ControlFailure(404,"FIRMWARE_NOT_FOUND"); return rows.getFirst();
    }
    private String compatibility(Map<String,Object> image,JsonNode info) {
        var app=info.path("application"); var chip=info.path("chip_info");
        if(!app.path("elf_sha256").asText().matches("[a-f0-9]{64}") || app.path("version").asText().isEmpty()
                || !chip.path("revision").isIntegralNumber() || !info.path("flash_size").isIntegralNumber()
                || !info.path("board").path("type").isTextual()) return "hardware_unknown";
        if(!image.get("board_type").equals(info.path("board").path("type").asText()) || chip.path("model").asInt(-1)!=18) return "incompatible_board";
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
    private Map<String,Object> sessionRequest(String action,String device,JsonNode session) {
        if(!session.path("session_id").isTextual() || session.path("session_id").asText().length()>128
                || !session.path("permission_version").isIntegralNumber()) throw new ControlFailure(503,"BRIDGE_UNAVAILABLE");
        return new LinkedHashMap<>(Map.of("action",action,"device_id",device,"session_id",session.path("session_id").asText(),
                "permission_version",session.path("permission_version").asLong()));
    }
    private long number(Map<String,Object> row,String name) { return ((Number)row.get(name)).longValue(); }
    private String encode(Object value) { try { return json.writeValueAsString(value); } catch(Exception invalid) { throw new IllegalStateException(); } }
    private static String id() { return UUID.randomUUID().toString().replace("-",""); }
    private static void validateId(String value) { if(value==null || !value.matches("[a-f0-9]{32}")) throw new ControlFailure(400,"INVALID_REQUEST"); }
    private static void validateDevice(String value) { if(value==null || !value.matches("[A-Za-z0-9_.:-]{1,128}")) throw new ControlFailure(400,"INVALID_REQUEST"); }
    public Map<String,Object> devices(int page,int limit) {
        if(page<1 || page>100000 || limit<1 || limit>100) throw new ControlFailure(400,"INVALID_PAGE");
        var rows=db.queryForList("SELECT device_id FROM dc_device ORDER BY device_id LIMIT ? OFFSET ?",limit,(page-1)*limit);
        var statuses=statuses(rows.stream().map(row->(String)row.get("device_id")).toList());
        for(var row:rows) {
            String status=statuses.get((String)row.get("device_id"));
            row.put("display_name",row.get("device_id")); row.put("status",status);
            row.put("reason",status); row.put("checked_at_ms",System.currentTimeMillis());
        }
        return Map.of("items",rows,"total",db.queryForObject("SELECT COUNT(*) FROM dc_device",Long.class),"page",page,"limit",limit);
    }
    public Map<String,String> statuses(List<String> devices) {
        var result=new LinkedHashMap<String,String>();
        if(devices.isEmpty()) return result;
        String first=status(devices.getFirst());
        // A failed bridge probe fails this inventory closed without N network timeouts.
        if(first.equals("bridge_unavailable")) { for(String device:devices) result.put(device,first); return result; }
        result.put(devices.getFirst(),first);
        var pending=new LinkedHashMap<String,CompletableFuture<String>>();
        for(String device:devices.subList(1,devices.size())) pending.put(device,CompletableFuture.supplyAsync(()->status(device),statusWorkers));
        for(var entry:pending.entrySet()) {
            try { result.put(entry.getKey(),entry.getValue().get()); }
            catch(InterruptedException interrupted) { Thread.currentThread().interrupt(); throw new ControlFailure(503,"BRIDGE_UNAVAILABLE"); }
            catch(ExecutionException unavailable) { result.put(entry.getKey(),"bridge_unavailable"); }
        }
        return result;
    }
    private String status(String device) {
        try { return bridge.call(Map.of("action","status","device_id",device)).path("status").asText(); }
        catch(ControlFailure unavailable) { return "bridge_unavailable"; }
    }
}
