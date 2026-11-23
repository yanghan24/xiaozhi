package xiaozhi.modules.speaker;

import java.util.Map;
import org.springframework.dao.DuplicateKeyException;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;

import static xiaozhi.modules.speaker.SpeakerSupport.hash;
import static xiaozhi.modules.speaker.SpeakerSupport.token;
import static xiaozhi.modules.speaker.SpeakerSupport.yes;

/** First-network registration. A pending proof can authenticate only the enrollment and login routes. */
@Service
public class SpeakerEnrollment {
    private final JdbcTemplate db;
    private final SpeakerDevices devices;
    private final SpeakerCrypto crypto;

    public SpeakerEnrollment(JdbcTemplate db, SpeakerDevices devices, SpeakerCrypto crypto) {
        this.db = db;
        this.devices = devices;
        this.crypto = crypto;
    }

    Map<String, Object> beginOrClaim(String authorization, String id, String board, String firmware) {
        var proof = SpeakerDevices.parseIdentity(authorization, id);
        if (!id.matches("enr-[a-f0-9]{32}") || board == null || board.isBlank()) {
            throw new SpeakerFailure(400, "INVALID_ENROLLMENT");
        }
        try {
            return devices.transactions.execute(tx -> beginOrResume(proof, board, firmware, true));
        } catch (DuplicateKeyException concurrentOrExistingDevice) {
            // A lost response or a concurrent first request can resume only the same proof.
            return devices.transactions.execute(tx -> beginOrResume(proof, board, firmware, false));
        }
    }

    private Map<String, Object> beginOrResume(SpeakerDevices.Identity proof, String board, String firmware,
                                               boolean create) {
        String id = proof.id();
        var rows = db.queryForList("SELECT e.proof_hash,e.receipt_cipher,d.admitted,s.customer_id,"
                + "s.admin_enabled,s.use_enabled FROM dc_device d JOIN speaker_device_state s ON s.device_id=d.device_id "
                + "JOIN speaker_enrollment e ON e.device_id=d.device_id WHERE d.device_id=? FOR UPDATE", id);
        if (rows.isEmpty()) {
            if (!create) throw new SpeakerFailure(403, "ENROLLMENT_DENIED");
            long now = System.currentTimeMillis();
            db.update("INSERT INTO dc_device(device_id,credential_hash,admitted) VALUES (?,?,FALSE)", id, proof.hash());
            db.update("INSERT INTO speaker_device_state(device_id,name,board_type,firmware_version,created_at_ms) "
                    + "VALUES (?,?,?,?,?)", id, "小智音箱 " + id.substring(id.length() - 6), board, firmware, now);
            db.update("INSERT INTO speaker_enrollment(device_id,proof_hash) VALUES (?,?)", id, proof.hash());
            devices.event(id, "registered", "auto-enrollment");
            return pending(id);
        }

        var row = rows.getFirst();
        if (!proof.hash().equals(row.get("proof_hash")) || !yes(row.get("admin_enabled"))) {
            throw new SpeakerFailure(403, "ENROLLMENT_DENIED");
        }
        if (row.get("receipt_cipher") instanceof String receipt) {
            if (!yes(row.get("admitted")) || row.get("customer_id") == null || !yes(row.get("use_enabled"))) {
                throw new SpeakerFailure(403, "ENROLLMENT_DENIED");
            }
            return issued(id, crypto.decryptEnrollmentCredential(receipt, id));
        }
        if (row.get("customer_id") == null || !yes(row.get("use_enabled"))) return pending(id);

        String credential = token();
        String receipt = crypto.encryptEnrollmentCredential(credential, id);
        db.update("UPDATE dc_device SET credential_hash=?,admitted=TRUE WHERE device_id=?", hash(credential), id);
        db.update("UPDATE speaker_enrollment SET receipt_cipher=? WHERE device_id=?", receipt, id);
        devices.event(id, "credential_issued", "auto-enrollment");
        return issued(id, credential);
    }

    void acknowledge(SpeakerDevices.Identity identity) {
        devices.transactions.executeWithoutResult(tx -> {
            devices.load(identity, true);
            db.update("DELETE FROM speaker_enrollment WHERE device_id=? AND receipt_cipher IS NOT NULL", identity.id());
        });
    }

    private static Map<String, Object> pending(String id) {
        return Map.of("device_id", id, "status", "pending");
    }

    private static Map<String, Object> issued(String id, String credential) {
        return Map.of("device_id", id, "status", "issued", "credential", credential);
    }
}
