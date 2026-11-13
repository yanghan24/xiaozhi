package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletInputStream;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import java.io.ByteArrayInputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.Arrays;
import org.mockito.ArgumentCaptor;
import org.junit.jupiter.api.Test;

class DeviceControlBodyFilterTest {
    @Test
    void leavesLargeFirmwareMultipartBodyForMvc() throws Exception {
        byte[] body = new byte[8192];
        Arrays.fill(body, (byte) 7);
        HttpServletRequest request = mock(HttpServletRequest.class);
        when(request.getServletPath()).thenReturn("/admin/product-ota/firmware/upload");
        when(request.getContentType()).thenReturn("multipart/form-data; boundary=fixture");
        when(request.getContentLengthLong()).thenReturn((long) body.length);
        when(request.getInputStream()).thenReturn(stream(body));
        FilterChain chain = mock(FilterChain.class);

        new DeviceControlBodyFilter().doFilter(request, mock(HttpServletResponse.class), chain);

        ArgumentCaptor<HttpServletRequest> forwarded = ArgumentCaptor.forClass(HttpServletRequest.class);
        verify(chain).doFilter(forwarded.capture(), any(HttpServletResponse.class));
        assertArrayEquals(body, forwarded.getValue().getInputStream().readAllBytes());
    }

    @Test
    void keepsSmallJsonLimitForOtherProductOtaRequests() throws Exception {
        byte[] body = new byte[4097];
        HttpServletRequest request = mock(HttpServletRequest.class);
        when(request.getServletPath()).thenReturn("/admin/product-ota/firmware");
        when(request.getContentType()).thenReturn("application/json");
        when(request.getContentLengthLong()).thenReturn((long) body.length);
        when(request.getInputStream()).thenReturn(stream(body));
        HttpServletResponse response = mock(HttpServletResponse.class);
        when(response.getWriter()).thenReturn(new PrintWriter(new StringWriter()));

        new DeviceControlBodyFilter().doFilter(request, response, mock(FilterChain.class));

        verify(response).setStatus(400);
        verify(response).flushBuffer();
    }

    private static ServletInputStream stream(byte[] body) {
        var input = new ByteArrayInputStream(body);
        return new ServletInputStream() {
            @Override public int read() { return input.read(); }
            @Override public int read(byte[] bytes, int offset, int length) { return input.read(bytes, offset, length); }
            @Override public boolean isFinished() { return input.available() == 0; }
            @Override public boolean isReady() { return true; }
            @Override public void setReadListener(jakarta.servlet.ReadListener listener) { }
        };
    }
}
