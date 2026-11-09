package xiaozhi.modules.customer;

import java.nio.file.*;
import java.util.*;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.jdbc.core.JdbcTemplate;
import xiaozhi.AdminApplication;

/** Test classpath only. Starts the real application with the controlled external market. */
public final class CustomerBrowserHarness {
    public static void main(String[] args) throws Exception {
        var market=new ControlledMarket();
        var app=new SpringApplicationBuilder(AdminApplication.class).run("--server.port=8002", "--server.servlet.context-path=/xiaozhi",
                "--customer.oidc.config-file="+market.config, "--renren.xss.enabled=true",
                "--device-control.enabled=true", "--device-control.public-origin=https://bridge.hncig.cn:18443",
                "--server.forward-headers-strategy=native");
        var db=app.getBean(JdbcTemplate.class);
        List<String> handles=new ArrayList<>();
        for (int i=0;i<3;i++) {
            String device="browser-"+UUID.randomUUID(), credential=CustomerSecrets.random();
            db.update("INSERT INTO dc_device(device_id,credential_hash) VALUES (?,?)",device,CustomerSecrets.hash(credential));
            var request=java.net.http.HttpRequest.newBuilder(java.net.URI.create("http://127.0.0.1:8002/xiaozhi/device-control/v1/requests"))
                    .header("Authorization","Bearer "+credential).header("Device-Id",device)
                    .header("X-Forwarded-Proto","https").header("Idempotency-Key",UUID.randomUUID().toString())
                    .header("Content-Type","application/json")
                    .POST(java.net.http.HttpRequest.BodyPublishers.ofString("{\"intent\":\"auto\",\"expected_permission_version\":1}")).build();
            var issued=java.net.http.HttpClient.newHttpClient().send(request,java.net.http.HttpResponse.BodyHandlers.ofString());
            if (issued.statusCode()!=200) throw new IllegalStateException("Device request issuance failed");
            String uri=new com.fasterxml.jackson.databind.ObjectMapper().readTree(issued.body()).path("data").path("binding_request").path("verification_uri").asText();
            handles.add(uri.substring(uri.lastIndexOf('/')+1));
        }
        Files.writeString(Path.of("target/browser-fixture.json"),new com.fasterxml.jackson.databind.ObjectMapper().writeValueAsString(Map.of("handles",handles)));
    }
}
