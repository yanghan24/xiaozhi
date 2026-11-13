package xiaozhi.modules.devicecontrol;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Set;
import jakarta.servlet.http.HttpServletRequest;
import org.springframework.http.ResponseEntity;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.web.bind.annotation.*;

@RestController
public class ProductOtaDownloadController {
    private final JdbcTemplate db;
    private final ProductOtaDownloads downloads;
    public ProductOtaDownloadController(JdbcTemplate db,ProductOtaDownloads downloads) { this.db=db; this.downloads=downloads; }
    @GetMapping("/ota-artifacts/{operation}/{sha}.bin")
    public ResponseEntity<byte[]> download(@PathVariable String operation,@PathVariable String sha,HttpServletRequest request) {
        if(!request.isSecure()) throw new ControlFailure(403,"HTTPS_REQUIRED");
        try {
            if(!operation.matches("[a-f0-9]{32}") || !sha.matches("[a-f0-9]{64}")
                    || !request.getParameterMap().keySet().equals(Set.of("expires","signature"))) throw new IllegalArgumentException();
            for(var values:request.getParameterMap().values()) if(values.length!=1) throw new IllegalArgumentException();
            String expiry=request.getParameter("expires"),signature=request.getParameter("signature");
            if(!expiry.matches("[0-9]{10}") || !signature.matches("[a-f0-9]{64}")) throw new IllegalArgumentException();
            long expires=Long.parseLong(expiry),now=System.currentTimeMillis()/1000;
            if(expires<=now || expires>now+900 || !MessageDigest.isEqual(signature.getBytes(StandardCharsets.US_ASCII),
                    downloads.signature(operation,sha,expires).getBytes(StandardCharsets.US_ASCII))) throw new IllegalArgumentException();
            var rows=db.queryForList("SELECT f.image,f.sha256,f.image_size FROM dc_ota_operation o JOIN dc_ota_firmware f ON f.firmware_id=o.firmware_id "
                    +"WHERE o.operation_id=? AND o.artifact_sha256=? AND o.download_expires=? AND o.active_device IS NOT NULL "
                    +"AND o.status IN ('dispatch_reserved','accepted_unconfirmed','result_unknown','result_requires_verification') AND f.verification_status='verified'",operation,sha,expires);
            if(rows.size()!=1) throw new IllegalArgumentException();
            var row=rows.getFirst(); byte[] bytes=(byte[])row.get("image");
            if(!sha.equals(row.get("sha256")) || bytes.length!=((Number)row.get("image_size")).intValue()
                    || !sha.equals(ProductOtaCatalog.digest(bytes))) throw new IllegalArgumentException();
            return ResponseEntity.ok().header("Cache-Control","no-store").header("X-Content-Type-Options","nosniff")
                    .header("Content-Type","application/octet-stream").contentLength(bytes.length).body(bytes);
        } catch(Exception denied) { throw new ControlFailure(404,"ARTIFACT_NOT_AVAILABLE"); }
    }
}
