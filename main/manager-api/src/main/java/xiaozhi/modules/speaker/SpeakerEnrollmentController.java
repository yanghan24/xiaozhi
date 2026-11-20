package xiaozhi.modules.speaker;

import jakarta.validation.Valid;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.http.CacheControl;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestHeader;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/speaker/v1/enrollment")
@ConditionalOnProperty(name = {"speaker.enabled", "speaker.enrollment-enabled"}, havingValue = "true")
public class SpeakerEnrollmentController {
    private final SpeakerEnrollment enrollment;
    private final SpeakerDevices devices;

    public SpeakerEnrollmentController(SpeakerEnrollment enrollment, SpeakerDevices devices) {
        this.enrollment = enrollment;
        this.devices = devices;
    }

    public record Registration(@NotBlank @Size(max = 128) String board_type,
                               @Size(max = 128) String firmware_version) {}

    @PostMapping
    public ResponseEntity<?> register(@RequestHeader(value = "Authorization", required = false) String authorization,
                                      @RequestHeader(value = "Device-Id", required = false) String id,
                                      @Valid @RequestBody Registration body) {
        return ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .body(enrollment.beginOrClaim(authorization, id, body.board_type(), body.firmware_version()));
    }

    @PostMapping("/ack")
    public ResponseEntity<Void> acknowledge(@RequestHeader(value = "Authorization", required = false) String authorization,
                                             @RequestHeader(value = "Device-Id", required = false) String id) {
        enrollment.acknowledge(devices.authenticate(authorization, id));
        return ResponseEntity.noContent().build();
    }
}
