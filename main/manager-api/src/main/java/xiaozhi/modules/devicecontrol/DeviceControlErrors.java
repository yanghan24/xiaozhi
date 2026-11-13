package xiaozhi.modules.devicecontrol;

import java.util.Map;
import org.springframework.core.annotation.Order;
import org.springframework.http.ResponseEntity;
import org.springframework.http.converter.HttpMessageNotReadableException;
import org.springframework.web.HttpMediaTypeNotSupportedException;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;

/** Keep product error envelopes and HTTP statuses separate from upstream Result. */
@Order(-100)
@RestControllerAdvice(assignableTypes = {DeviceControlController.class, DeviceDeliveryController.class, ProductOtaCatalogController.class, ProductOtaPushController.class, ProductOtaDownloadController.class})
public class DeviceControlErrors {
    @ExceptionHandler(ControlFailure.class)
    public ResponseEntity<Map<String, Object>> failure(ControlFailure failure) {
        var response = ResponseEntity.status(failure.status).header("Cache-Control", "no-store");
        if (failure.status == 429) response.header("Retry-After", "1");
        return response.body(failure.body());
    }

    @ExceptionHandler({HttpMessageNotReadableException.class, HttpMediaTypeNotSupportedException.class})
    public ResponseEntity<Map<String, Object>> invalid(Exception exception) {
        return failure(new ControlFailure(400, "INVALID_REQUEST"));
    }

    @ExceptionHandler(org.apache.shiro.authz.AuthorizationException.class)
    public ResponseEntity<Map<String, Object>> forbidden(Exception exception) {
        return failure(new ControlFailure(403, "OPERATOR_REQUIRED"));
    }

    @ExceptionHandler(Exception.class)
    public ResponseEntity<Map<String, Object>> unavailable(Exception exception) {
        // Do not log SQL values, bearer credentials or opaque binding handles.
        return failure(new ControlFailure(503, "TEMPORARILY_UNAVAILABLE"));
    }
}
