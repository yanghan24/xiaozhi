package xiaozhi.modules.speaker;

import java.io.IOException;
import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.core.annotation.Order;
import org.springframework.stereotype.Component;
import org.springframework.web.filter.OncePerRequestFilter;

/** In product mode the administrator console does not expose customer registration or legacy binding pages. */
@Component
@Order(-200)
@ConditionalOnProperty(name="speaker.enabled",havingValue="true")
public class SpeakerManagementGuard extends OncePerRequestFilter {
    @Override protected void doFilterInternal(HttpServletRequest request,HttpServletResponse response,FilterChain chain)
            throws ServletException,IOException {
        String path=request.getRequestURI().substring(request.getContextPath().length());
        if(path.startsWith("/customer/") || path.startsWith("/bind/") || java.util.Set.of(
                "/user/register","/user/smsVerification","/user/retrieve-password").contains(path)) {
            response.setStatus(403);response.setContentType("application/json;charset=UTF-8");
            response.getWriter().write("{\"code\":\"ADMIN_CONSOLE_ONLY\"}");return;
        }
        if(path.startsWith("/speaker/") || path.startsWith("/admin/speaker/")) response.setHeader("Cache-Control","no-store");
        chain.doFilter(request,response);
    }
}
