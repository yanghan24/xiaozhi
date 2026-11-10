package xiaozhi.modules.customer;

import java.nio.file.*;
import java.util.*;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.boot.web.servlet.context.ServletWebServerApplicationContext;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.boot.web.client.RestTemplateBuilder;
import org.springframework.jdbc.core.JdbcTemplate;
import xiaozhi.AdminApplication;

/** Isolated real HTTP authority for the Python WebSocket regression. */
public final class CustomerVoiceHarness {
    public static void main(String[] args) throws Exception {
        var market=new ControlledMarket();
        var app=(ServletWebServerApplicationContext)new SpringApplicationBuilder(AdminApplication.class).run(
                "--server.port=8002","--server.servlet.context-path=/xiaozhi","--server.forward-headers-strategy=native",
                "--customer.oidc.config-file="+market.config,"--device-control.enabled=true",
                "--device-control.public-origin=https://bridge.hncig.cn:18443","--device-control.voice-enabled=true",
                "--device-control.model-base-url=http://127.0.0.1:18085/v1/",
                "--device-control.voice-service-secret=isolated-voice-test-service-secret-123456");
        var client=new BindingTestClient(new TestRestTemplate(new RestTemplateBuilder().rootUri("http://127.0.0.1:8002/xiaozhi")),app.getBean(JdbcTemplate.class),market);
        List<Map<String,Object>> devices=new ArrayList<>();
        for(int i=0;i<3;i++) {
            var device=client.device();var flow=client.scan(device);client.login(flow,null,null);
            var bound=BindingTestClient.data(client.confirm(flow,"bind"));var voice=(Map<?,?>)client.state(device).get("voice");
            devices.add(Map.of("device_id",device.id(),"credential",device.credential(),"voice_token",voice.get("token"),
                    "cookie",flow.headers().getFirst("Cookie"),"csrf",flow.headers().getFirst("X-CSRF-Token"),"device_session_id",bound.get("device_session_id")));
        }
        Files.writeString(Path.of("target/voice-fixture.json"),new com.fasterxml.jackson.databind.ObjectMapper().writeValueAsString(Map.of("devices",devices)));
    }
}
