package xiaozhi.modules.devicecontrol;

import java.io.BufferedReader;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ReadListener;
import jakarta.servlet.ServletException;
import jakarta.servlet.ServletInputStream;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletRequestWrapper;
import jakarta.servlet.http.HttpServletResponse;
import org.springframework.web.filter.OncePerRequestFilter;
import com.fasterxml.jackson.databind.ObjectMapper;

/** Bounds bytes before either XSS sanitization or MVC can buffer a chunked upload. */
final class DeviceControlBodyFilter extends OncePerRequestFilter {
    private static final int MAX_BYTES = 4096;
    private static final String FIRMWARE_UPLOAD_PATH = "/admin/product-ota/firmware/upload";
    private final ObjectMapper json = new ObjectMapper();

    @Override
    protected void doFilterInternal(HttpServletRequest request, HttpServletResponse response, FilterChain chain)
            throws ServletException, IOException {
        // Multipart firmware uploads are bounded by Spring's multipart limits and
        // validated by ProductOtaCatalog. Reading them here would truncate every
        // application image to the small JSON request limit below.
        if (FIRMWARE_UPLOAD_PATH.equals(request.getServletPath())
                && request.getContentType() != null
                && request.getContentType().toLowerCase(java.util.Locale.ROOT).startsWith("multipart/form-data")) {
            chain.doFilter(request, response);
            return;
        }
        int maxBytes = "/admin/product-ota/batch-preview".equals(request.getServletPath()) ? 256 * 1024
                : "/device-control/v1/maintenance".equals(request.getServletPath()) ? 16 * 1024 : MAX_BYTES;
        if (request.getContentLengthLong() > maxBytes) {
            reject(response);
            return;
        }
        byte[] body = request.getInputStream().readNBytes(maxBytes + 1);
        if (body.length > maxBytes) {
            reject(response);
            return;
        }
        var input = new ByteArrayInputStream(body);
        var stream = new ServletInputStream() {
            @Override public int read() { return input.read(); }
            @Override public int read(byte[] bytes, int offset, int length) { return input.read(bytes, offset, length); }
            @Override public boolean isFinished() { return input.available() == 0; }
            @Override public boolean isReady() { return true; }
            @Override public void setReadListener(ReadListener listener) {
                throw new IllegalStateException("Asynchronous request body reads are not supported");
            }
        };
        chain.doFilter(new HttpServletRequestWrapper(request) {
            @Override public ServletInputStream getInputStream() { return stream; }
            @Override public BufferedReader getReader() {
                return new BufferedReader(new InputStreamReader(stream, StandardCharsets.UTF_8));
            }
        }, response);
    }

    private void reject(HttpServletResponse response) throws IOException {
        response.setStatus(400);
        response.setHeader("Cache-Control", "no-store");
        response.setHeader("Connection", "close");
        response.setContentType("application/json;charset=UTF-8");
        json.writeValue(response.getWriter(), new ControlFailure(400, "INVALID_REQUEST").body());
        response.flushBuffer();
    }
}
