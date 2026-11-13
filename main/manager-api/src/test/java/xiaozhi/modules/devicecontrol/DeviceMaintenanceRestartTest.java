package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.*;
import org.junit.jupiter.api.Test;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.boot.context.logging.LoggingApplicationListener;
import org.springframework.boot.web.servlet.context.ServletWebServerApplicationContext;
import org.springframework.jdbc.core.JdbcTemplate;
import xiaozhi.AdminApplication;

/** Restart actual HTTP processes against the same isolated database, without a voice service. */
class DeviceMaintenanceRestartTest {
    ServletWebServerApplicationContext start() {
        var builder=new SpringApplicationBuilder(AdminApplication.class);
        builder.application().setListeners(builder.application().getListeners().stream()
                .filter(listener -> !(listener instanceof LoggingApplicationListener)).toList());
        return (ServletWebServerApplicationContext)builder.run("--server.port=0","--server.servlet.context-path=/xiaozhi",
                "--server.forward-headers-strategy=native","--device-control.enabled=true",
                "--device-control.public-origin=https://speaker.example.test",
                "--product.ota.bridge-url=http://127.0.0.1:9/internal/product-ota/v1/control",
                "--product.ota.bridge-key=fixture-maintenance-key-97-123456789",
                "--product.ota.download-origin=https://firmware.example.test",
                "--product.ota.download-key=fixture-download-key-97-1234567890");
    }
    ProductOtaPushHttpTest client(ServletWebServerApplicationContext app) {
        var client=new ProductOtaPushHttpTest();
        client.port=app.getWebServer().getPort(); client.db=app.getBean(JdbcTemplate.class);
        return client;
    }
    @Test void waitingCommandAndAcceptedReceiptRemainTheSameAcrossApiRestarts() throws Exception {
        String boot="1".repeat(32),id; long expires;
        try(var first=start()) {
            var client=client(first); client.setup(); client.admitManagement(boot);
            id=client.managementOperation().path("operation_id").asText();
            assertEquals("waiting_idle",client.operationData(id).path("status").asText());
        }
        try(var second=start()) {
            var client=client(second);
            assertEquals("waiting_idle",client.operationData(id).path("status").asText());
            var command=ProductOtaPushHttpTest.JSON.readTree(client.maintenance(ProductOtaPushHttpTest.maintenanceReport(boot,true,null)).body()).path("data").path("command");
            assertEquals(id,command.path("operation_id").asText()); expires=command.path("expires_at_ms").asLong();
            var response=client.maintenance(ProductOtaPushHttpTest.maintenanceReport(boot,true,client.receipt(id,boot,"accepted")));
            assertEquals(200,response.statusCode(),response.body());
        }
        try(var third=start()) {
            var client=client(third);
            try {
                assertEquals("accepted_unconfirmed",client.operationData(id).path("status").asText());
                var response=client.maintenance(ProductOtaPushHttpTest.maintenanceReport(boot,true,client.receipt(id,boot,"accepted")));
                assertEquals(200,response.statusCode(),response.body());
                var data=ProductOtaPushHttpTest.JSON.readTree(response.body()).path("data");
                assertTrue(data.path("command").isNull());
                assertEquals(expires,data.path("operation_ack").path("expires_at_ms").asLong());
                ProductOtaPushHttpTest.elf="c".repeat(64);
                assertEquals(200,client.maintenance(ProductOtaPushHttpTest.maintenanceReport("2".repeat(32),true,client.receipt(id,boot,"installed"))).statusCode());
                assertEquals("target_running",client.operationData(id).path("observation_status").asText());
            } finally { client.cleanup(); }
        }
    }
}
