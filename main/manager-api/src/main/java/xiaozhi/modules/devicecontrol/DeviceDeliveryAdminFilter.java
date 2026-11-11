package xiaozhi.modules.devicecontrol;

import java.io.IOException;
import java.util.Collections;
import com.fasterxml.jackson.databind.ObjectMapper;
import jakarta.servlet.ServletRequest;
import jakarta.servlet.ServletResponse;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.apache.shiro.authc.AuthenticationException;
import org.apache.shiro.authc.AuthenticationToken;
import xiaozhi.modules.security.oauth2.Oauth2Filter;

/** Reuses the operator realm while giving the new protocol proper HTTP authentication failures. */
public final class DeviceDeliveryAdminFilter extends Oauth2Filter {
    private final ObjectMapper json = new ObjectMapper();

    @Override
    protected boolean onAccessDenied(ServletRequest request, ServletResponse response) throws Exception {
        var http = (HttpServletRequest) request;
        if (!http.isSecure()) return reject(response, 403, "HTTPS_REQUIRED");
        if (Collections.list(http.getHeaders("Authorization")).size() != 1) {
            return reject(response, 401, "OPERATOR_AUTH_REQUIRED");
        }
        String authorization = http.getHeader("Authorization");
        if (authorization == null || !authorization.startsWith("Bearer ") || authorization.length() > 4096) {
            return reject(response, 401, "OPERATOR_AUTH_REQUIRED");
        }
        return executeLogin(request, response);
    }

    @Override
    protected boolean onLoginFailure(AuthenticationToken token, AuthenticationException error,
            ServletRequest request, ServletResponse response) {
        try { return reject(response, 401, "OPERATOR_AUTH_REQUIRED"); }
        catch (IOException unavailable) { return false; }
    }

    private boolean reject(ServletResponse response, int status, String code) throws IOException {
        var http = (HttpServletResponse) response;
        http.setStatus(status);
        http.setHeader("Cache-Control", "no-store");
        http.setContentType("application/json;charset=UTF-8");
        json.writeValue(http.getWriter(), new ControlFailure(status, code).body());
        return false;
    }
}
