package xiaozhi.modules.devicecontrol;

import java.time.Clock;
import java.util.*;
import com.fasterxml.jackson.databind.*;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;

/** Durable, device-authenticated maintenance. Customer binding and voice permission are not authority here. */
@Service
public class DeviceMaintenance {
    private final JdbcTemplate db;
    private final Clock clock;
    private final DevicePresence presence;
    private final ProductOtaHardware hardware;
    private final ProductOtaCatalog catalog;
    private final ProductOtaDownloads downloads;
    private final TransactionTemplate transactions;
    private final ObjectMapper json=new ObjectMapper();

    public DeviceMaintenance(JdbcTemplate db,PlatformTransactionManager tx,@Qualifier("deviceControlClock") Clock clock,
            DevicePresence presence,ProductOtaHardware hardware,ProductOtaCatalog catalog,ProductOtaDownloads downloads) {
        this.db=db; this.clock=clock; this.presence=presence; this.hardware=hardware; this.catalog=catalog;
        this.downloads=downloads; this.transactions=new TransactionTemplate(tx);
    }

    JsonNode info(Map<String,Object> contact) { return decode((String)contact.get("system_info")); }

    Map<String,Object> snapshot(Map<String,Object> contact) {
        return new LinkedHashMap<>(Map.of("ota_transport","management","boot_id",contact.get("boot_id"),
                "credential_hash",contact.get("credential_hash"),"hardware_fingerprint",fingerprint(info(contact))));
    }

    private String fingerprint(JsonNode system) {
        var hardware=json.createObjectNode();
        for(String field:List.of("flash_size","chip_info","board")) hardware.set(field,system.path(field));
        var partitions=new ArrayList<JsonNode>(); system.path("partition_table").forEach(partitions::add);
        partitions.sort(Comparator.comparing(item -> item.path("label").asText()));
        hardware.set("partition_table",json.valueToTree(partitions));
        hardware.put("active_slot",system.path("ota").path("label").asText());
        return ProductOtaCatalog.digest(hardware.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
    }

    void arm(String operation) {
        transactions.executeWithoutResult(tx -> {
            var row=db.queryForMap("SELECT * FROM dc_ota_operation WHERE operation_id=?",operation);
            db.queryForList("SELECT device_id FROM dc_device WHERE device_id=? FOR UPDATE",row.get("device_id"));
            String reason=preflight(row,presence.read((String)row.get("device_id")));
            boolean ready="eligible".equals(reason);
            db.update("UPDATE dc_ota_operation SET status=?,reason=?,active_device=?,updated_at_ms=? WHERE operation_id=? AND status='dispatch_reserved'",
                    ready ? "waiting_idle" : "not_sent",ready ? "waiting_idle" : reason,ready ? row.get("device_id") : null,clock.millis(),operation);
        });
    }

    /** Expiry never turns an exposed command into an un-sent command or releases its unique device claim. */
    void expire() {
        db.update("UPDATE dc_ota_operation SET active_device=CASE WHEN status='waiting_idle' THEN NULL ELSE active_device END,"
                +"reason='command_expired',updated_at_ms=?,status=CASE WHEN status='waiting_idle' THEN 'not_sent' ELSE 'result_unknown' END "
                +"WHERE ota_transport='management' AND status IN ('waiting_idle','command_offered') AND download_expires<=?",clock.millis(),clock.millis()/1000);
    }

    Map<String,Object> exchange(DeviceControlService.Identity identity,JsonNode body) {
        JsonNode report=validate(body);
        return transactions.execute(tx -> {
            presence.lock(identity);
            presence.report(identity,body,report);
            expire();
            var result=new LinkedHashMap<String,Object>();
            result.put("server_time_ms",clock.millis());
            result.put("operation_ack",body.path("operation").isNull() ? null : acknowledge(identity,body,report));
            observeRunning(identity,body,report);
            result.put("command",offer(identity,body));
            return result;
        });
    }

    private void observeRunning(DeviceControlService.Identity identity,JsonNode body,JsonNode report) {
        if(!"valid".equals(report.path("ota").path("state").asText())) return;
        var rows=db.queryForList("SELECT * FROM dc_ota_operation WHERE active_device=? AND status IN "
                +"('command_offered','accepted_unconfirmed','result_unknown','result_requires_verification') FOR UPDATE",identity.deviceId());
        for(var row:rows) {
            var original=decode((String)row.get("snapshot"));
            if(!identity.hash().equals(original.path("credential_hash").asText())
                    || !row.get("target_elf_sha256").equals(report.path("application").path("elf_sha256").asText())
                    || original.path("current_elf_sha256").asText().equals(row.get("target_elf_sha256"))
                    || !original.path("current_elf_sha256").asText().matches("[a-f0-9]{64}")
                    || clock.millis()<number(row,"created_at_ms")) continue;
            if("management".equals(row.get("ota_transport")) && body.path("boot_id").asText().equals(original.path("boot_id").asText())) continue;
            // A voice bootstrap cannot persist the new management receipt in old firmware.
            // A fresh authenticated target report is independent running evidence, not a transport ACK.
            db.update("UPDATE dc_ota_operation SET observation_status='target_running',observed_at_ms=?,active_device=NULL,updated_at_ms=? WHERE operation_id=?",
                    clock.millis(),clock.millis(),row.get("operation_id"));
        }
    }

    private Object offer(DeviceControlService.Identity identity,JsonNode body) {
        var rows=db.queryForList("SELECT * FROM dc_ota_operation WHERE active_device=? AND ota_transport='management' FOR UPDATE",identity.deviceId());
        if(rows.isEmpty()) return null;
        var row=rows.getFirst();
        if(!Set.of("waiting_idle","command_offered").contains(row.get("status"))) return null;
        String compatible=preflight(row,presence.read(identity.deviceId()));
        if(!"eligible".equals(compatible)) {
            boolean exposed="command_offered".equals(row.get("status"));
            db.update("UPDATE dc_ota_operation SET status=?,reason=?,active_device=?,updated_at_ms=? WHERE operation_id=?",
                    exposed ? "result_unknown" : "not_sent",compatible,exposed ? identity.deviceId() : null,clock.millis(),row.get("operation_id"));
            return null;
        }
        if(!body.path("idle").asBoolean()) return null;
        long expires=number(row,"download_expires")*1000;
        if(expires<=clock.millis()) return null;
        var image=hardware.firmware((String)row.get("firmware_id"));
        // Commit exposure before the HTTP response. Lost responses may return this exact command, never a new operation.
        db.update("UPDATE dc_ota_operation SET status='command_offered',reason='awaiting_device_acceptance',updated_at_ms=? WHERE operation_id=?",
                clock.millis(),row.get("operation_id"));
        return Map.of("operation_id",row.get("operation_id"),"boot_id",body.path("boot_id").asText(),
                "url",downloads.url((String)row.get("operation_id"),(String)row.get("artifact_sha256"),number(row,"download_expires")),
                "artifact_sha256",row.get("artifact_sha256"),"target_elf_sha256",row.get("target_elf_sha256"),
                "image_size",image.get("image_size"),"expires_at_ms",expires,"expires_in_ms",expires-clock.millis());
    }

    private Object acknowledge(DeviceControlService.Identity identity,JsonNode body,JsonNode report) {
        var receipt=body.path("operation");
        String id=receipt.path("operation_id").asText(),status=receipt.path("status").asText();
        var rows=db.queryForList("SELECT * FROM dc_ota_operation WHERE operation_id=? AND device_id=? AND ota_transport='management' FOR UPDATE",id,identity.deviceId());
        if(rows.size()!=1) throw new ControlFailure(409,"OPERATION_CHANGED");
        var row=rows.getFirst(); var original=decode((String)row.get("snapshot"));
        if(!identity.hash().equals(original.path("credential_hash").asText())
                || !receipt.path("origin_boot_id").asText().equals(original.path("boot_id").asText()))
            throw new ControlFailure(409,"OPERATION_CHANGED");
        long expires=number(row,"download_expires")*1000;
        // Terminal receipt retries acknowledge only their original operation; they cannot mutate a newer device claim.
        if(status.equals(row.get("device_status")) && Set.of("failed","interrupted","installed").contains(status))
            return ack(id,status,expires);
        if("target_running".equals(row.get("observation_status")) && "installed".equals(status)
                && !body.path("boot_id").asText().equals(original.path("boot_id").asText())
                && "valid".equals(report.path("ota").path("state").asText())
                && row.get("target_elf_sha256").equals(report.path("application").path("elf_sha256").asText())) {
            db.update("UPDATE dc_ota_operation SET device_status='installed' WHERE operation_id=?",id);
            return ack(id,status,expires);
        }
        if("target_running".equals(row.get("observation_status")) || Set.of("failed","not_sent").contains(row.get("status")))
            throw new ControlFailure(409,"OPERATION_CHANGED");
        if("accepted".equals(status)) {
            if(!Set.of("command_offered","accepted_unconfirmed","result_unknown").contains(row.get("status"))
                    || !body.path("boot_id").asText().equals(original.path("boot_id").asText()))
                throw new ControlFailure(409,"OPERATION_CHANGED");
            String compatible=preflight(row,presence.read(identity.deviceId()));
            if(!body.path("idle").asBoolean() || !"eligible".equals(compatible))
                throw new ControlFailure(409,"OPERATION_CHANGED");
            if(expires>clock.millis())
                db.update("UPDATE dc_ota_operation SET status='accepted_unconfirmed',reason='awaiting_installation',device_status='accepted',updated_at_ms=? WHERE operation_id=?",clock.millis(),id);
            return ack(id,status,expires);
        }
        if(!Set.of("command_offered","accepted_unconfirmed","result_unknown","result_requires_verification").contains(row.get("status")))
            throw new ControlFailure(409,"OPERATION_CHANGED");
        boolean newBoot=!body.path("boot_id").asText().equals(original.path("boot_id").asText());
        if("installed".equals(status)) {
            if(!newBoot || !"valid".equals(report.path("ota").path("state").asText())
                    || !row.get("target_elf_sha256").equals(report.path("application").path("elf_sha256").asText()))
                throw new ControlFailure(409,"INSTALLATION_NOT_VERIFIED");
            db.update("UPDATE dc_ota_operation SET observation_status='target_running',observed_at_ms=?,active_device=NULL,"
                    +"device_status='installed',updated_at_ms=? WHERE operation_id=?",clock.millis(),clock.millis(),id);
        } else {
            if("interrupted".equals(status) && !newBoot) throw new ControlFailure(409,"OPERATION_CHANGED");
            if("failed".equals(status) && newBoot && "valid".equals(report.path("ota").path("state").asText())
                    && row.get("target_elf_sha256").equals(report.path("application").path("elf_sha256").asText())) {
                // A durable failure receipt may outlive a reboot. Keep independently verified
                // running-target evidence even when the local receipt still says failed.
                db.update("UPDATE dc_ota_operation SET observation_status='target_running',observed_at_ms=?,active_device=NULL,"
                        +"device_status='failed',updated_at_ms=? WHERE operation_id=?",clock.millis(),clock.millis(),id);
                return ack(id,status,expires);
            }
            db.update("UPDATE dc_ota_operation SET status=?,reason=?,device_status=?,active_device=?,updated_at_ms=? WHERE operation_id=?",
                    "failed".equals(status) ? "failed" : "result_requires_verification",
                    "failed".equals(status) ? "device_upgrade_failed" : "device_restart_interrupted",status,
                    "failed".equals(status) ? null : identity.deviceId(),clock.millis(),id);
        }
        return ack(id,status,expires);
    }

    private Map<String,Object> ack(String id,String status,long expires) {
        return Map.of("operation_id",id,"status",status,"expires_at_ms",expires,"expires_in_ms",Math.max(0,expires-clock.millis()));
    }

    private String preflight(Map<String,Object> operation,Map<String,Object> contact) {
        if(!presence.capable(contact)) return "management_unavailable";
        var snapshot=decode((String)operation.get("snapshot"));
        if(!snapshot.path("boot_id").asText().equals(contact.get("boot_id"))
                || !snapshot.path("credential_hash").asText().equals(contact.get("credential_hash"))) return "session_changed";
        var system=info(contact);
        if(!snapshot.path("hardware_fingerprint").asText().equals(fingerprint(system))) return "hardware_changed";
        if(!snapshot.path("current_elf_sha256").asText().equals(system.path("application").path("elf_sha256").asText())) return "session_changed";
        if(!"valid".equals(system.path("ota").path("state").asText())) return "ota_not_valid";
        try {
            catalog.operator(number(operation,"operator_id"));
            var image=hardware.firmware((String)operation.get("firmware_id"));
            if(!snapshot.path("sha256").asText().equals(image.get("sha256"))
                    || !operation.get("artifact_sha256").equals(image.get("sha256"))
                    || !operation.get("target_elf_sha256").equals(image.get("elf_sha256"))) return "artifact_changed";
            return hardware.compatibility((String)operation.get("device_id"),image,system);
        } catch(ControlFailure unavailable) { return "maintenance_not_authorized"; }
    }

    /** Validate before recording presence, and store only the hardware/app projection. */
    private JsonNode validate(JsonNode body) {
        fields(body,Set.of("boot_id","ota_capable","idle","system_info","operation"));
        if(!body.path("boot_id").isTextual() || !body.path("boot_id").asText().matches("[a-f0-9]{32}") || !body.path("ota_capable").isBoolean()
                || !body.path("idle").isBoolean()) invalid();
        var operation=body.path("operation");
        if(!operation.isNull()) {
            fields(operation,Set.of("operation_id","origin_boot_id","status"));
            if(!operation.path("operation_id").isTextual() || !operation.path("operation_id").asText().matches("[a-f0-9]{32}")
                    || !operation.path("origin_boot_id").isTextual() || !operation.path("origin_boot_id").asText().matches("[a-f0-9]{32}")
                    || !Set.of("accepted","failed","interrupted","installed").contains(operation.path("status").asText())) invalid();
        }
        var supplied=body.path("system_info");
        if(!supplied.isObject()) invalid();
        var system=json.createObjectNode();
        system.set("flash_size",integer(supplied.path("flash_size")));
        var chip=system.putObject("chip_info");
        chip.set("model",integer(supplied.path("chip_info").path("model")));
        chip.set("revision",integer(supplied.path("chip_info").path("revision")));
        for(String field:List.of("cores","features")) if(supplied.path("chip_info").has(field))
            chip.set(field,integer(supplied.path("chip_info").path(field)));
        var app=system.putObject("application");
        app.put("version",text(supplied.path("application").path("version"),32));
        String elf=text(supplied.path("application").path("elf_sha256"),64);
        if(!elf.matches("[a-f0-9]{64}")) invalid();
        app.put("elf_sha256",elf);
        system.putObject("board").put("type",text(supplied.path("board").path("type"),128));
        var ota=system.putObject("ota");
        ota.put("label",text(supplied.path("ota").path("label"),16));
        String state=text(supplied.path("ota").path("state"),32);
        if(!Set.of("valid","pending_verify","unknown").contains(state)) invalid();
        ota.put("state",state);
        var partitions=supplied.path("partition_table");
        if(!partitions.isArray() || partitions.isEmpty() || partitions.size()>32) invalid();
        var table=system.putArray("partition_table");
        for(var item:partitions) {
            var partition=table.addObject();
            partition.put("label",text(item.path("label"),16));
            for(String field:List.of("type","subtype","address","size")) partition.set(field,integer(item.path(field)));
        }
        return system;
    }

    private JsonNode integer(JsonNode value) {
        if(!value.isIntegralNumber() || !value.canConvertToLong() || value.asLong()<0 || value.asLong()>0xffffffffL) invalid();
        return value;
    }
    private String text(JsonNode value,int max) {
        if(!value.isTextual() || value.asText().isEmpty() || value.asText().length()>max
                || !value.asText().matches("[\\x20-\\x7e]+")) invalid();
        return value.asText();
    }
    private void fields(JsonNode body,Set<String> fields) {
        if(body==null || !body.isObject() || body.size()!=fields.size()) invalid();
        body.fieldNames().forEachRemaining(field -> { if(!fields.contains(field)) invalid(); });
    }
    private void invalid() { throw new ControlFailure(400,"INVALID_REQUEST"); }
    private JsonNode decode(String raw) { try { return json.readTree(raw); } catch(Exception bad) { throw new IllegalStateException(bad); } }
    private long number(Map<String,Object> row,String name) { return ((Number)row.get(name)).longValue(); }
}
