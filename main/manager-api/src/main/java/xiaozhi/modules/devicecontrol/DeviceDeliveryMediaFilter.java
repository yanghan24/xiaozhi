package xiaozhi.modules.devicecontrol;

import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import java.io.IOException;
import java.util.Collections;
import org.springframework.http.MediaType;
import org.springframework.web.filter.OncePerRequestFilter;

/** MVC rejects consumes before selecting a controller, so its scoped advice cannot handle it. */
final class DeviceDeliveryMediaFilter extends OncePerRequestFilter {
    private final ObjectMapper json = new ObjectMapper();

    @Override
    protected void doFilterInternal(HttpServletRequest request, HttpServletResponse response, FilterChain chain)
            throws ServletException, IOException {
        if ("POST".equals(request.getMethod()) && !isJson(request)) {
            response.setStatus(400);
            response.setHeader("Cache-Control", "no-store");
            response.setHeader("Connection", "close");
            response.setContentType("application/json;charset=UTF-8");
            json.writeValue(response.getWriter(), new ControlFailure(400, "INVALID_REQUEST").body());
            response.flushBuffer();
            return;
        }
        chain.doFilter(request, response);
    }

    private boolean isJson(HttpServletRequest request) {
        var headers = Collections.list(request.getHeaders("Content-Type"));
        if (headers.size() != 1) return false;
        try {
            var media = MediaType.parseMediaType(headers.getFirst());
            return "application".equalsIgnoreCase(media.getType()) && "json".equalsIgnoreCase(media.getSubtype());
        } catch (IllegalArgumentException invalid) {
            return false;
        }
    }
}
