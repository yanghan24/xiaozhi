package xiaozhi.modules.devicecontrol;

import java.io.IOException;
import jakarta.servlet.ServletRequest;
import jakarta.servlet.ServletResponse;
import jakarta.servlet.http.HttpServletResponse;
import jakarta.servlet.http.HttpServletRequest;
import org.apache.shiro.web.filter.PathMatchingFilter;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.Collections;
import java.util.Set;
import org.springframework.http.MediaType;

/** Device credentials never fall through to administrator authentication. */
public class DeviceCredentialFilter extends PathMatchingFilter {
    static final String IDENTITY = DeviceCredentialFilter.class.getName();
    private final DeviceControlService service;
    private final ObjectMapper json = new ObjectMapper();

    public DeviceCredentialFilter(DeviceControlService service) {
        this.service = service;
    }

    @Override
    protected boolean onPreHandle(ServletRequest request, ServletResponse response, Object mappedValue)
            throws IOException {
        var http = (HttpServletResponse) response;
        http.setContentType("application/json;charset=UTF-8");
        http.setHeader("Cache-Control", "no-store");
        var req = (HttpServletRequest) request;
        try {
            var identity = service.authenticate(req.getHeader("Authorization"), req.getHeader("Device-Id"));
            if (!req.isSecure()) throw new ControlFailure(403, "DEVICE_NOT_ADMITTED");
            validateRequest(req);
            request.setAttribute(IDENTITY, identity);
            return true;
        } catch (ControlFailure failure) {
            http.setStatus(failure.status);
            json.writeValue(http.getWriter(), failure.body());
            return false;
        } catch (RuntimeException failure) {
            http.setStatus(503);
            json.writeValue(http.getWriter(), new ControlFailure(503, "TEMPORARILY_UNAVAILABLE").body());
            return false;
        }
    }

    private void validateRequest(HttpServletRequest request) {
        for (String header : Set.of("Authorization", "Device-Id", "Idempotency-Key", "Content-Type")) {
            if (Collections.list(request.getHeaders(header)).size() > 1) throw new ControlFailure(400, "INVALID_REQUEST");
        }
        String path = request.getRequestURI().substring(request.getContextPath().length());
        if (!path.startsWith("/device-control/v1/")) throw new ControlFailure(400, "UNSUPPORTED_VERSION");
        boolean state = path.equals("/device-control/v1/state") && request.getMethod().equals("GET");
        boolean account = path.equals("/device-control/v1/account") && request.getMethod().equals("GET");
        boolean mutation = Set.of("/device-control/v1/requests", "/device-control/v1/state-acks",
                "/device-control/v1/sign-out", "/device-control/v1/unbind", "/device-control/v1/maintenance").contains(path)
                && request.getMethod().equals("POST");
        if (!state && !account && !mutation) throw new ControlFailure(400, "INVALID_REQUEST");
        if (mutation) {
            try {
                MediaType type = MediaType.parseMediaType(request.getContentType() == null ? "" : request.getContentType());
                if (!"application".equalsIgnoreCase(type.getType()) || !"json".equalsIgnoreCase(type.getSubtype())) {
                    throw new ControlFailure(400, "INVALID_REQUEST");
                }
            } catch (IllegalArgumentException invalid) {
                throw new ControlFailure(400, "INVALID_REQUEST");
            }
        }
        var allowedQuery = state ? Set.of("after_revision", "wait_ms") : Set.<String>of();
        request.getParameterMap().forEach((key, values) -> {
            if (!allowedQuery.contains(key) || values.length != 1) throw new ControlFailure(400, "INVALID_REQUEST");
        });
    }
}
