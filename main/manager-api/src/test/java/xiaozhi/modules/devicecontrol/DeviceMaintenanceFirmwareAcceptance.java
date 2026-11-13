package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;

/** Explicit interop seam: production C++ request/parser -> actual Java HTTP -> C++ accepted request. */
class DeviceMaintenanceFirmwareAcceptance {
    String firmware(String... args) throws Exception {
        String binary=System.getProperty("maintenance.firmware.binary");
        assertNotNull(binary,"Run explicitly with the compiled maintenance_tests executable mounted in this test container");
        var command=new java.util.ArrayList<String>(); command.add(binary); command.addAll(java.util.List.of(args));
        var process=new ProcessBuilder(command).redirectError(ProcessBuilder.Redirect.INHERIT).start();
        if(!process.waitFor(10,TimeUnit.SECONDS)) { process.destroyForcibly(); fail("Firmware protocol helper timed out"); }
        String output=new String(process.getInputStream().readAllBytes(),StandardCharsets.UTF_8);
        assertEquals(0,process.exitValue(),output); return output;
    }
    @Test void productionFirmwareCanReceiveAndAcknowledgeTheActualServerCommand() throws Exception {
        var fixture=new DeviceMaintenanceRestartTest();
        try(var app=fixture.start()) {
            var client=fixture.client(app); client.setup();
            try {
                client.db.update("UPDATE dc_device SET credential_hash=? WHERE device_id='ota97-device'",
                        ProductOtaCatalog.digest(ProductOtaPushHttpTest.DEVICE_CREDENTIAL.getBytes(StandardCharsets.UTF_8)));
                var report=ProductOtaPushHttpTest.JSON.readTree(firmware("--fixture"));
                var contact=client.maintenance(report); assertEquals(200,contact.statusCode(),contact.body());
                String id=client.managementOperation().path("operation_id").asText();
                var response=client.maintenance(report); assertEquals(200,response.statusCode(),response.body());
                Path responsePath=Path.of("target/maintenance-interop-command.json").toAbsolutePath();
                Files.writeString(responsePath,response.body());
                var accepted=ProductOtaPushHttpTest.JSON.readTree(firmware("--accept-response",responsePath.toString(),"https://firmware.example.test"));
                assertEquals(id,accepted.path("operation").path("operation_id").asText());
                assertEquals("accepted",accepted.path("operation").path("status").asText());
                var ack=client.maintenance(accepted); assertEquals(200,ack.statusCode(),ack.body());
                Files.writeString(Path.of("target/maintenance-interop-ack.json"),ack.body());
                var data=ProductOtaPushHttpTest.JSON.readTree(ack.body()).path("data");
                assertTrue(data.path("command").isNull());
                assertEquals(id,data.path("operation_ack").path("operation_id").asText());
                assertEquals("accepted",data.path("operation_ack").path("status").asText());
                assertTrue(data.path("operation_ack").path("expires_in_ms").asLong()>0);
                assertEquals("accepted_unconfirmed",client.operationData(id).path("status").asText());
            } finally { client.cleanup(); }
        }
    }
}
