package xiaozhi.modules.speaker;

import java.util.Map;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

@RestControllerAdvice(basePackages="xiaozhi.modules.speaker")
@org.springframework.core.annotation.Order(-100)
public class SpeakerErrors {
    @ExceptionHandler(SpeakerFailure.class)
    ResponseEntity<?> expected(SpeakerFailure failure) {
        return ResponseEntity.status(failure.status).header("Cache-Control","no-store")
                .body(Map.of("code",failure.getMessage()));
    }
    @ExceptionHandler({org.springframework.web.bind.MethodArgumentNotValidException.class,
            org.springframework.http.converter.HttpMessageNotReadableException.class})
    ResponseEntity<?> invalid(Exception failure) {
        return ResponseEntity.badRequest().body(Map.of("code","INVALID_REQUEST"));
    }
    @ExceptionHandler(org.apache.shiro.authz.AuthorizationException.class)
    ResponseEntity<?> forbidden(Exception failure) {
        return ResponseEntity.status(403).header("Cache-Control","no-store").body(Map.of("code","ADMIN_CONSOLE_ONLY"));
    }
    @ExceptionHandler(Exception.class)
    ResponseEntity<?> unavailable(Exception failure) {
        return ResponseEntity.status(503).header("Cache-Control","no-store")
                .body(Map.of("code","SPEAKER_UNAVAILABLE"));
    }
}
