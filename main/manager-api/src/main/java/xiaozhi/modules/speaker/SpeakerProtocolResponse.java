package xiaozhi.modules.speaker;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.core.MethodParameter;
import org.springframework.http.MediaType;
import org.springframework.http.converter.HttpMessageConverter;
import org.springframework.http.converter.json.MappingJackson2HttpMessageConverter;
import org.springframework.http.server.ServerHttpRequest;
import org.springframework.http.server.ServerHttpResponse;
import org.springframework.web.bind.annotation.RestControllerAdvice;
import org.springframework.web.servlet.mvc.method.annotation.ResponseBodyAdvice;

/** The device/voice contract uses JSON numbers. Keep the legacy UI's Long-to-string mapper outside this module. */
@RestControllerAdvice(basePackages="xiaozhi.modules.speaker")
public class SpeakerProtocolResponse implements ResponseBodyAdvice<Object> {
    private final ObjectMapper protocolJson=new ObjectMapper();
    @Override public boolean supports(MethodParameter type,Class<? extends HttpMessageConverter<?>> converter) {
        return MappingJackson2HttpMessageConverter.class.isAssignableFrom(converter);
    }
    @Override public Object beforeBodyWrite(Object body,MethodParameter type,MediaType contentType,
            Class<? extends HttpMessageConverter<?>> converter,ServerHttpRequest request,ServerHttpResponse response) {
        return protocolJson.valueToTree(body);
    }
}
