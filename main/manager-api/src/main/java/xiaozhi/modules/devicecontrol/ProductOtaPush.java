package xiaozhi.modules.devicecontrol;

import java.util.*;
import java.time.Clock;
import org.springframework.beans.factory.annotation.Qualifier;
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
    private final Clock clock;
    private final ProductOtaBridge bridge;
    private final DevicePresence presence;
    private final DeviceMaintenance maintenance;
    private final ProductOtaHardware hardware;
    private final ObjectMapper json=new ObjectMapper();
    private final ProductOtaDownloads downloads;
    private final ProductOtaCatalog catalog;
    private final TransactionTemplate transactions;
    private final ExecutorService statusWorkers=Executors.newFixedThreadPool(8);
    @PreDestroy public void close() { statusWorkers.shutdownNow(); }
    private static final String PUBLIC_FIELDS="operation_id,preview_id,request_key,device_id,firmware_id,artifact_sha256,target_version,target_elf_sha256,ota_transport,status,reason,observation_status,observed_at_ms,created_at_ms,updated_at_ms";
    public ProductOtaPush(JdbcTemplate db, ProductOtaBridge bridge,ProductOtaDownloads downloads,
            ProductOtaCatalog catalog,PlatformTransactionManager tx, @Qualifier("deviceControlClock") Clock clock, DevicePresence presence, DeviceMaintenance maintenance, ProductOtaHardware hardware) {
        this.clock=clock; this.db=db; this.bridge=bridge; this.downloads=downloads; this.catalog=catalog; this.transactions=new TransactionTemplate(tx);
        this.presence=presence; this.maintenance=maintenance; this.hardware=hardware;
    }
    public Map<String,Object> preview(long operator,String device,String firmware) {
        validateDevice(device); validateId(firmware);
        if(db.queryForList("SELECT device_id FROM dc_device WHERE device_id=?",device).size()!=1)
            throw new ControlFailure(404,"DEVICE_NOT_FOUND");
        var image=hardware.firmware(firmware);
        var result=new LinkedHashMap<String,Object>();
        result.put("device_id",device); result.put("firmware_id",firmware);
        result.put("target_version",image.get("app_version")); result.put("target_elf_sha256",image.get("elf_sha256"));
        result.put("eligible",false); result.put("reason","bridge_unavailable");
        result.put("current_version",""); result.put("current_elf_sha256","");
        result.put("preview_id",""); result.put("expires_at_ms",0); result.put("ota_transport",null);
        if(pending(device)) { result.put("reason","operation_pending"); return result; }
        try {
            var contact=presence.read(device);
            boolean managed=presence.capable(contact);
            var session=managed ? json.valueToTree(Map.of("status","current")) : bridge.call(Map.of("action","status","device_id",device));
            if(!session.path("status").asText().equals("current")) {
                result.put("reason",displayStatus(session.path("status").asText(),contact)); return result;
            }
            result.put("ota_transport",managed ? "management" : "voice");
            var request=managed ? maintenance.snapshot(contact) : sessionRequest("inspect",device,session);
            var inspected=managed ? json.valueToTree(Map.of("status","ready","system_info",maintenance.info(contact))) : bridge.call(request);
            if(!inspected.path("status").asText().equals("ready")) { result.put("reason",inspected.path("status").asText()); return result; }
            var info=inspected.path("system_info");
            result.put("current_version",info.path("application").path("version").asText());
            result.put("current_elf_sha256",info.path("application").path("elf_sha256").asText());
            String reason=hardware.compatibility(device,image,info);
            if(managed && !"valid".equals(info.path("ota").path("state").asText())) reason="ota_not_valid";
            result.put("reason",reason);
            if(!reason.equals("eligible")) return result;
            String id=id(); long expires=clock.millis()+120000;
            var snapshot=new LinkedHashMap<String,Object>(request);
            snapshot.put("sha256",image.get("sha256")); snapshot.put("current_elf_sha256",result.get("current_elf_sha256"));
            if(!managed) snapshot.put("credential_hash",db.queryForMap("SELECT credential_hash FROM dc_device WHERE device_id=?",device).get("credential_hash"));
            db.update("INSERT INTO dc_ota_preview VALUES (?,?,?,?,?,?)",id,operator,device,firmware,encode(snapshot),expires);
            result.put("eligible",true); result.put("preview_id",id); result.put("expires_at_ms",expires);
        } catch(ControlFailure unavailable) { result.put("reason","bridge_unavailable"); }
        return result;
    }
    private boolean pending(String device) {
        return !db.queryForList("SELECT operation_id FROM dc_ota_operation WHERE active_device=?",device).isEmpty();
    }
    public List<Map<String,Object>> operations() {
        maintenance.expire();
        return db.queryForList("SELECT "+PUBLIC_FIELDS+" FROM dc_ota_operation ORDER BY created_at_ms DESC,operation_id");
    }
    public Map<String,Object> operation(String operation) {
        validateId(operation);
        maintenance.expire();
        var rows=db.queryForList("SELECT "+PUBLIC_FIELDS+" FROM dc_ota_operation WHERE operation_id=?",operation);
        if(rows.size()!=1) throw new ControlFailure(404,"OPERATION_NOT_FOUND");
        return rows.getFirst();
    }
    /** Observe authenticated running code separately; this path can never dispatch. */
    public Map<String,Object> observe(String operation) {
        var visible=operation(operation);
        if("management".equals(visible.get("ota_transport"))) return visible;
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
                if(at>=number(visible,"created_at_ms") && at<=clock.millis()+5000)
                    db.update("UPDATE dc_ota_operation SET observation_status='target_running',observed_at_ms=?,active_device=NULL,updated_at_ms=? WHERE operation_id=?",
                            at,clock.millis(),operation);
            }
        } catch(Exception unavailable) { /* Keep the durable original result when observation is unavailable. */ }
        return operation(operation);
    }
    private record Reserved(String id,boolean fresh) { }
    public Map<String,Object> confirm(long operator,String preview,String requestKey) {
        return confirm(operator,preview,requestKey,false);
    }
    /** Only an already accepted, fixed batch can outlive its confirmation window. */
    Map<String,Object> confirmBatchTarget(long operator,String batch,String preview) {
        var rows=db.queryForList("SELECT snapshot FROM dc_ota_batch WHERE batch_id=? AND operator_id=? AND status='started'",batch,operator);
        boolean authorized=false;
        if(rows.size()==1) try {
            for(var item:json.readTree((String)rows.getFirst().get("snapshot")))
                if(item.path("eligible").asBoolean() && preview.equals(item.path("preview_id").asText())) authorized=true;
        } catch(Exception corrupt) { throw new IllegalStateException(corrupt); }
        if(!authorized) throw new ControlFailure(409,"BATCH_NOT_STARTED");
        return confirm(operator,preview,preview,true);
    }
    private Map<String,Object> confirm(long operator,String preview,String requestKey,boolean acceptedBatch) {
        validateId(preview); validateId(requestKey);
        var prior=db.queryForList("SELECT operation_id,preview_id FROM dc_ota_operation WHERE operator_id=? AND request_key=?",operator,requestKey);
        if(!prior.isEmpty()) {
            if(!preview.equals(prior.getFirst().get("preview_id"))) throw new ControlFailure(409,"REQUEST_CONFLICT");
            return operation((String)prior.getFirst().get("operation_id"));
        }
        var previews=db.queryForList("SELECT * FROM dc_ota_preview WHERE preview_id=? AND operator_id=?",preview,operator);
        if(previews.size()!=1) throw new ControlFailure(404,"PREVIEW_NOT_FOUND");
        var chosen=previews.getFirst(); String device=(String)chosen.get("device_id");
        var image=hardware.firmware((String)chosen.get("firmware_id"));
        // Validate configuration before reservation; a missing download service cannot create a live command.
        downloads.signature("0".repeat(32),(String)image.get("sha256"),clock.millis()/1000+600);
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
            if(!acceptedBatch && number(chosen,"expires_at_ms")<=clock.millis()) throw new ControlFailure(409,"PREVIEW_EXPIRED");
            String operation=id(); long now=clock.millis();
            db.update("INSERT INTO dc_ota_operation(operation_id,preview_id,request_key,operator_id,device_id,active_device,firmware_id,artifact_sha256,target_version,target_elf_sha256,snapshot,status,reason,created_at_ms,updated_at_ms,download_expires,ota_transport) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    operation,preview,requestKey,operator,device,device,chosen.get("firmware_id"),image.get("sha256"),image.get("app_version"),image.get("elf_sha256"),chosen.get("snapshot"),"dispatch_reserved","awaiting_result",now,now,now/1000+600,decode((String)chosen.get("snapshot")).path("ota_transport").asText("voice"));
            return new Reserved(operation,true);
        });
        if(!reservation.fresh()) return operation(reservation.id());
        if("management".equals(decode((String)chosen.get("snapshot")).path("ota_transport").asText())) {
            maintenance.arm(reservation.id());
            return operation(reservation.id());
        }
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
                String compatible=checked.path("status").asText().equals("ready") ? hardware.compatibility(device,image,checked.path("system_info")) : checked.path("status").asText();
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
                status,safeReason(reason),status.equals("not_sent") ? null : device,clock.millis(),reservation.id());
        return operation(reservation.id());
    }
    private JsonNode decode(String raw) { try { return json.readTree(raw); } catch(Exception bad) { throw new IllegalStateException(bad); } }
    private String safeReason(String reason) { return reason.matches("[a-z_]{1,64}") ? reason : "result_requires_verification"; }
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
            row.put("reason",status); row.put("checked_at_ms",clock.millis());
            var contact=presence.read((String)row.get("device_id"));
            row.put("online",presence.online(contact) || "current".equals(status));
            row.put("last_seen_at_ms",contact.get("last_seen_at_ms"));
            row.put("ota_transport",presence.capable(contact) ? "management" : "current".equals(status) ? "voice" : null);
            row.put("voice_status",presence.capable(contact) ? "not_checked" : status.equals("online_requires_wake") ? "offline" : status);
        }
        return Map.of("items",rows,"total",db.queryForObject("SELECT COUNT(*) FROM dc_device",Long.class),"page",page,"limit",limit);
    }
    public Map<String,String> statuses(List<String> devices) {
        var result=new LinkedHashMap<String,String>();
        var contacts=new LinkedHashMap<String,Map<String,Object>>();
        var legacy=new ArrayList<String>();
        for(String device:devices) {
            var contact=presence.read(device); contacts.put(device,contact);
            if(presence.capable(contact)) result.put(device,"current");
            else legacy.add(device);
        }
        if(legacy.isEmpty()) return result;
        String first=voiceStatus(legacy.getFirst());
        // Bridge availability is independent of management presence. One failed probe must
        // not fan out N timeouts merely because the first device still has a heartbeat.
        if(first.equals("bridge_unavailable")) {
            for(String device:legacy) result.put(device,first);
            return result;
        }
        result.put(legacy.getFirst(),displayStatus(first,contacts.get(legacy.getFirst())));
        var pending=new LinkedHashMap<String,CompletableFuture<String>>();
        for(String device:legacy.subList(1,legacy.size()))
            pending.put(device,CompletableFuture.supplyAsync(()->voiceStatus(device),statusWorkers));
        for(var entry:pending.entrySet()) {
            try { result.put(entry.getKey(),displayStatus(entry.getValue().get(),contacts.get(entry.getKey()))); }
            catch(InterruptedException interrupted) { Thread.currentThread().interrupt(); throw new ControlFailure(503,"BRIDGE_UNAVAILABLE"); }
            catch(ExecutionException unavailable) { result.put(entry.getKey(),"bridge_unavailable"); }
        }
        return result;
    }
    private String displayStatus(String voice,Map<String,Object> contact) {
        return "offline".equals(voice) && presence.online(contact) ? "online_requires_wake" : voice;
    }
    private String voiceStatus(String device) {
        try { return bridge.call(Map.of("action","status","device_id",device)).path("status").asText(); }
        catch(ControlFailure unavailable) { return "bridge_unavailable"; }
    }
}
