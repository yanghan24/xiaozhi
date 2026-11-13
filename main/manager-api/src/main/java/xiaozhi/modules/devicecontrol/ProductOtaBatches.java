package xiaozhi.modules.devicecontrol;

import java.util.*;
import java.time.Clock;
import org.springframework.beans.factory.annotation.Qualifier;
import java.util.concurrent.*;
import com.fasterxml.jackson.databind.*;
import jakarta.annotation.PreDestroy;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.ApplicationArguments;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;

/** A batch is a fixed set of single-device previews, claimed once and never resumed. */
@Service
public class ProductOtaBatches implements ApplicationRunner {
    private final JdbcTemplate db;
    private final Clock clock;
    private final ProductOtaPush push;
    private final TransactionTemplate transactions;
    private final ObjectMapper json = new ObjectMapper();
    private final ExecutorService workers = Executors.newFixedThreadPool(4);
    private final ExecutorService coordinators = Executors.newFixedThreadPool(2);
    public ProductOtaBatches(JdbcTemplate db, ProductOtaPush push, PlatformTransactionManager tx, @Qualifier("deviceControlClock") Clock clock) {
        this.clock=clock; this.db=db; this.push=push; this.transactions=new TransactionTemplate(tx);
    }
    @PreDestroy public void close() { coordinators.shutdownNow(); workers.shutdownNow(); }
    @Override public void run(ApplicationArguments arguments) {
        // The deployed manager has one active process. Startup only classifies abandoned work; it never resumes it.
        db.update("UPDATE dc_ota_batch SET status='failed' WHERE status='preparing'");
        db.update("UPDATE dc_ota_batch SET status='interrupted' WHERE status='started'");
        db.update("UPDATE dc_ota_operation SET status='result_unknown',reason='process_interrupted',updated_at_ms=? WHERE status='dispatch_reserved'",clock.millis());
    }
    public Map<String,Object> preview(long operator,String mode,List<String> devices,String firmware) {
        validateId(firmware);
        if(!Set.of("selected","all_online").contains(mode) || devices.size()>1000
                || (mode.equals("selected") && devices.isEmpty()) || (mode.equals("all_online") && !devices.isEmpty()))
            throw new ControlFailure(400,"INVALID_REQUEST");
        for(String device:devices) if(device==null || !device.matches("[A-Za-z0-9_.:-]{1,128}")) throw new ControlFailure(400,"INVALID_REQUEST");
        List<String> candidates=mode.equals("all_online")
                ? db.queryForList("SELECT device_id FROM dc_device ORDER BY device_id",String.class)
                : new ArrayList<>(new TreeSet<>(devices));
        String id=id();
        db.update("INSERT INTO dc_ota_batch VALUES (?,?,?,?,?,?,?,?,?)",id,operator,null,firmware,mode,"[]","preparing",0,clock.millis());
        var receipt=preview(operator,id);
        coordinators.submit(()->{
            try { prepare(operator,id,candidates,firmware); }
            catch(Exception unavailable) { db.update("UPDATE dc_ota_batch SET status='failed' WHERE batch_id=? AND status='preparing'",id); }
        });
        return receipt;
    }
    private void prepare(long operator,String id,List<String> candidates,String firmware) {
        List<CompletableFuture<Map<String,Object>>> tasks=new ArrayList<>();
        var statuses=push.statuses(candidates);
        for(String device:candidates) tasks.add(CompletableFuture.supplyAsync(()->
                "current".equals(statuses.get(device)) ? push.preview(operator,device,firmware)
                : Map.of("device_id",device,"firmware_id",firmware,"eligible",false,"reason",statuses.get(device)),workers));
        List<Map<String,Object>> items=new ArrayList<>();
        for(var task:tasks) {
            try { items.add(task.get()); }
            catch(InterruptedException interrupted) { Thread.currentThread().interrupt(); throw new ControlFailure(503,"PREVIEW_INTERRUPTED"); }
            catch(ExecutionException failed) {
                if(failed.getCause() instanceof ControlFailure failure) throw failure;
                throw new ControlFailure(503,"PREVIEW_UNAVAILABLE");
            }
        }
        long expires=clock.millis()+120000;
        transactions.executeWithoutResult(tx->{
            // Publish one usable confirmation window only after every target has been inspected.
            for(var item:items) if(Boolean.TRUE.equals(item.get("eligible"))) {
                db.update("UPDATE dc_ota_preview SET expires_at_ms=? WHERE preview_id=? AND operator_id=?",expires,item.get("preview_id"),operator);
                item.put("expires_at_ms",expires);
            }
            db.update("UPDATE dc_ota_batch SET snapshot=?,status='preview',expires_at_ms=? WHERE batch_id=? AND status='preparing'",encode(items),expires,id);
        });
    }
    public Map<String,Object> preview(long operator,String id) {
        validateId(id); var row=owned(operator,id); var items=items(row);
        long count=0; for(var item:items) if(item.path("eligible").asBoolean()) count++;
        String status=(String)row.get("status");
        return Map.of("preview_id",id,"firmware_id",row.get("firmware_id"),"items",items,"eligible_count",count,
                "expires_at_ms",row.get("expires_at_ms"),"status",status.equals("preview") ? "ready" : status,
                "reason",status.equals("failed") ? "preview_unavailable" : "");
    }
    public Map<String,Object> confirm(long operator,String preview,String key) {
        validateId(preview); validateId(key);
        boolean fresh=Boolean.TRUE.equals(transactions.execute(tx->{
            // Serialize request-key claims per operator; device reservations still arbitrate across operators.
            db.queryForList("SELECT id FROM sys_user WHERE id=? FOR UPDATE",operator);
            var prior=db.queryForList("SELECT batch_id FROM dc_ota_batch WHERE operator_id=? AND request_key=?",operator,key);
            if(!prior.isEmpty() && !preview.equals(prior.getFirst().get("batch_id"))) throw new ControlFailure(409,"REQUEST_CONFLICT");
            var row=owned(operator,preview);
            if(Set.of("preparing","failed").contains(row.get("status"))) throw new ControlFailure(409,"PREVIEW_NOT_READY");
            if(!"preview".equals(row.get("status"))) return false;
            if(((Number)row.get("expires_at_ms")).longValue()<=clock.millis()) throw new ControlFailure(409,"PREVIEW_EXPIRED");
            db.update("UPDATE dc_ota_batch SET status='started',request_key=? WHERE batch_id=?",key,preview);
            return true;
        }));
        if(fresh) {
            var receipt=batch(operator,preview);
            coordinators.submit(()->execute(operator,preview));
            return receipt;
        }
        return batch(operator,preview);
    }
    private void execute(long operator,String preview) {
        try {
            var snapshot=items(owned(operator,preview));
            List<CompletableFuture<Void>> tasks=new ArrayList<>();
            for(var item:snapshot) if(item.path("eligible").asBoolean()) tasks.add(CompletableFuture.runAsync(()->{
                try { push.confirmBatchTarget(operator,preview,item.path("preview_id").asText()); }
                catch(ControlFailure denied) {
                    // Another batch may own this device. Its durable operation remains authoritative.
                    // No retry, even when the competing operation later resolves.
                    ((com.fasterxml.jackson.databind.node.ObjectNode)item).put("failure_reason",denied.code.toLowerCase(Locale.ROOT));
                }
            },workers));
            for(var task:tasks) try { task.get(); }
            catch(ExecutionException failed) { /* Query preserves every durable operation. */ }
            catch(InterruptedException interrupted) { Thread.currentThread().interrupt(); throw new ControlFailure(503,"BATCH_INTERRUPTED"); }
            db.update("UPDATE dc_ota_batch SET status='finished',snapshot=? WHERE batch_id=? AND status='started'",encode(snapshot),preview);
        } catch(Exception interrupted) { db.update("UPDATE dc_ota_batch SET status='interrupted' WHERE batch_id=? AND status='started'",preview); }
    }
    public List<Map<String,Object>> batches(long operator) {
        var result=new ArrayList<Map<String,Object>>();
        for(String id:db.queryForList("SELECT batch_id FROM dc_ota_batch WHERE operator_id=? AND request_key IS NOT NULL ORDER BY created_at_ms DESC",String.class,operator)) result.add(batch(operator,id));
        return result;
    }
    public Map<String,Object> batch(long operator,String id) {
        validateId(id); var row=owned(operator,id); var results=new ArrayList<Map<String,Object>>();
        for(var item:items(row)) {
            var result=new LinkedHashMap<String,Object>();
            result.put("device_id",item.path("device_id").asText());
            result.put("ota_transport",item.path("ota_transport").isTextual() ? item.path("ota_transport").asText() : null);
            boolean eligible=item.path("eligible").asBoolean();
            boolean running="started".equals(row.get("status"));
            result.put("status",eligible && running ? "validating" : "not_sent");
            result.put("reason",eligible ? (running ? "batch_result_pending_no_retry" : item.path("failure_reason").asText("interrupted_before_send")) : item.path("reason").asText());
            if(eligible) {
                var operations=db.queryForList("SELECT operation_id FROM dc_ota_operation WHERE preview_id=?",item.path("preview_id").asText());
                if(!operations.isEmpty()) result.putAll(push.operation((String)operations.getFirst().get("operation_id")));
            }
            results.add(result);
        }
        return Map.of("batch_id",id,"status",row.get("status"),"firmware_id",row.get("firmware_id"),"items",results,"request_key",Objects.toString(row.get("request_key"),""));
    }
    private Map<String,Object> owned(long operator,String id) {
        var rows=db.queryForList("SELECT * FROM dc_ota_batch WHERE batch_id=? AND operator_id=?",id,operator);
        if(rows.size()!=1) throw new ControlFailure(404,"BATCH_NOT_FOUND"); return rows.getFirst();
    }
    private JsonNode items(Map<String,Object> row) {
        try { return json.readTree((String)row.get("snapshot")); } catch(Exception corrupt) { throw new IllegalStateException(corrupt); }
    }
    private String encode(Object value) { try { return json.writeValueAsString(value); } catch(Exception invalid) { throw new IllegalStateException(invalid); } }
    private static String id() { return UUID.randomUUID().toString().replace("-",""); }
    private static void validateId(String value) { if(value==null || !value.matches("[a-f0-9]{32}")) throw new ControlFailure(400,"INVALID_REQUEST"); }
}
