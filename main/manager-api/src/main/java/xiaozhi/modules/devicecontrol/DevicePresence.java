package xiaozhi.modules.devicecontrol;

import java.time.Clock;
import java.util.Map;
import com.fasterxml.jackson.databind.JsonNode;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;

/** Authenticated management contact, independent of customer voice permission. */
@Service
public class DevicePresence {
    static final long ONLINE_MS = 90000;
    private final JdbcTemplate db;
    private final Clock clock;
    private final TransactionTemplate transactions;

    public DevicePresence(JdbcTemplate db, PlatformTransactionManager tx,
            @Qualifier("deviceControlClock") Clock clock) {
        this.db=db; this.clock=clock; this.transactions=new TransactionTemplate(tx);
    }

    void touch(DeviceControlService.Identity identity) {
        transactions.executeWithoutResult(tx -> {
            lock(identity);
            var prior=db.queryForList("SELECT credential_hash FROM dc_device_presence WHERE device_id=?",identity.deviceId());
            if(!prior.isEmpty() && !identity.hash().equals(prior.getFirst().get("credential_hash")))
                db.update("DELETE FROM dc_device_presence WHERE device_id=?",identity.deviceId());
            db.update("INSERT INTO dc_device_presence(device_id,credential_hash,last_seen_at_ms) VALUES (?,?,?) "
                    +"ON DUPLICATE KEY UPDATE last_seen_at_ms=VALUES(last_seen_at_ms)",identity.deviceId(),identity.hash(),clock.millis());
        });
    }

    void lock(DeviceControlService.Identity identity) {
        if(db.queryForList("SELECT device_id FROM dc_device WHERE device_id=? AND credential_hash=? AND admitted=true FOR UPDATE",
                identity.deviceId(),identity.hash()).size()!=1) throw new ControlFailure(401,"INVALID_DEVICE_CREDENTIAL");
    }

    Map<String,Object> read(String device) {
        var rows=db.queryForList("SELECT p.* FROM dc_device_presence p JOIN dc_device d ON d.device_id=p.device_id "
                +"AND d.credential_hash=p.credential_hash AND d.admitted=true WHERE p.device_id=?",device);
        return rows.isEmpty() ? Map.of() : rows.getFirst();
    }

    boolean online(Map<String,Object> report) {
        Object at=report.get("last_seen_at_ms");
        return at instanceof Number && ((Number)at).longValue()>clock.millis()-ONLINE_MS
                && ((Number)at).longValue()<=clock.millis();
    }

    boolean capable(Map<String,Object> report) {
        Object at=report.get("report_at_ms");
        return online(report) && Boolean.TRUE.equals(report.get("ota_capable")) && at instanceof Number
                && ((Number)at).longValue()>clock.millis()-ONLINE_MS && ((Number)at).longValue()<=clock.millis();
    }

    /** Caller holds the device lock; remembered boots prevent a delayed request reviving a retired boot. */
    void report(DeviceControlService.Identity identity,JsonNode body,JsonNode info) {
        String boot=body.path("boot_id").asText();
        var current=read(identity.deviceId());
        if(!boot.equals(current.get("boot_id"))) {
            if(!db.queryForList("SELECT boot_id FROM dc_device_boot WHERE device_id=? AND credential_hash=? AND boot_id=?",
                    identity.deviceId(),identity.hash(),boot).isEmpty()) throw new ControlFailure(409,"STALE_DEVICE_BOOT");
            db.update("INSERT INTO dc_device_boot VALUES (?,?,?,?)",identity.deviceId(),identity.hash(),boot,clock.millis());
        }
        db.update("INSERT INTO dc_device_presence(device_id,credential_hash,last_seen_at_ms,report_at_ms,boot_id,ota_capable,idle,system_info) "
                +"VALUES (?,?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE credential_hash=VALUES(credential_hash),last_seen_at_ms=VALUES(last_seen_at_ms),"
                +"report_at_ms=VALUES(report_at_ms),boot_id=VALUES(boot_id),ota_capable=VALUES(ota_capable),idle=VALUES(idle),system_info=VALUES(system_info)",
                identity.deviceId(),identity.hash(),clock.millis(),clock.millis(),boot,body.path("ota_capable").asBoolean(),body.path("idle").asBoolean(),info.toString());
    }
}
