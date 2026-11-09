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
                "--customer.oidc.config-file="+market.config, "--renren.xss.enabled=true");
        var db=app.getBean(JdbcTemplate.class);
        List<String> handles=new ArrayList<>();
        for (int i=0;i<3;i++) {
            String device=UUID.randomUUID().toString().replace("-",""), handle=UUID.randomUUID().toString();
            db.update("INSERT INTO ai_device(id,alias) VALUES (?,?)",device,"浏览器测试音箱 "+(i+1));
            db.update("INSERT INTO customer_device_request(handle,device_id,purpose,permission_version,expires_at_ms) VALUES (?,?,'bind',1,?)",handle,device,System.currentTimeMillis()+1_800_000);
            handles.add(handle);
        }
        Files.writeString(Path.of("target/browser-fixture.json"),new com.fasterxml.jackson.databind.ObjectMapper().writeValueAsString(Map.of("handles",handles)));
    }
}
