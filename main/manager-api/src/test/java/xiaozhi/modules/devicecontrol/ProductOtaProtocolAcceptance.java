package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.*;
import com.fasterxml.jackson.databind.*;
import java.net.*;
import java.net.http.*;
import java.util.*;
import org.junit.jupiter.api.Test;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.boot.web.servlet.context.ServletWebServerApplicationContext;
import org.springframework.boot.context.logging.LoggingApplicationListener;
import org.springframework.jdbc.core.JdbcTemplate;
import xiaozhi.AdminApplication;

/** Explicit Ubuntu acceptance: real Java HTTP -> Python bridge -> WebSocket device peer.
 * Run -Dtest=ProductOtaProtocolAcceptance with the isolated ota_bridge_http.py service.
 */
class ProductOtaProtocolAcceptance {
    static final String KEY="fixture-maintenance-key-97-123456789";
    final ObjectMapper json=new ObjectMapper();
    final HttpClient http=HttpClient.newHttpClient();
    ServletWebServerApplicationContext start() {
        var builder=new SpringApplicationBuilder(AdminApplication.class);
        builder.application().setListeners(builder.application().getListeners().stream()
                .filter(listener -> !(listener instanceof LoggingApplicationListener)).toList());
        return (ServletWebServerApplicationContext)builder.run("--server.port=0","--server.servlet.context-path=/xiaozhi",
                "--product.ota.bridge-url=http://ota97-voice:8097/internal/product-ota/v1/control",
                "--product.ota.bridge-key="+KEY,"--product.ota.download-origin=https://firmware.example.test",
                "--product.ota.download-key=fixture-download-key-97-1234567890");
    }
    JsonNode fixture(Map<String,Object> command) throws Exception {
        var response=http.send(HttpRequest.newBuilder(URI.create("http://ota97-voice:8098/fixture"))
                .header("X-Fixture-Key",KEY).header("Content-Type","application/json")
                .POST(HttpRequest.BodyPublishers.ofString(json.writeValueAsString(command))).build(),HttpResponse.BodyHandlers.ofString());
        assertEquals(200,response.statusCode()); return json.readTree(response.body());
    }
    ProductOtaPushHttpTest client(ServletWebServerApplicationContext app) {
        var client=new ProductOtaPushHttpTest(); client.port=app.getWebServer().getPort(); client.db=app.getBean(JdbcTemplate.class); return client;
    }
    JsonNode data(HttpResponse<String> response) throws Exception {
        assertEquals(200,response.statusCode(),response.body()); return json.readTree(response.body()).path("data");
    }
    @Test void exactSessionSingleSendAndUnknownResultSurviveBothServiceRestarts() throws Exception {
        fixture(Map.of("action","reset")); String operation; Map<String,String> request;
        try(var first=start()) {
            var client=client(first); client.setup();
            client.db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-unselected')");
            var inventory=data(client.get("devices",ProductOtaPushHttpTest.TOKEN));
            var found=new HashSet<String>();
            for(int page=1;page<=((inventory.path("total").asInt()+49)/50);page++) {
                var devices=page==1 ? inventory.path("items") : data(client.get("devices?page="+page,ProductOtaPushHttpTest.TOKEN)).path("items");
                for(var device:devices) if(Set.of("ota97-device","ota97-unselected").contains(device.path("device_id").asText())) {
                    assertEquals("current",device.path("status").asText()); found.add(device.path("device_id").asText());
                }
            }
            assertEquals(Set.of("ota97-device","ota97-unselected"),found);
            var preview=data(client.post("preview",Map.of("device_id","ota97-device","firmware_id",ProductOtaPushHttpTest.FIRMWARE)));
            assertTrue(preview.path("eligible").asBoolean(),preview.toString());
            fixture(Map.of("action","device","device_id","ota97-device","session_id","changed-before-send"));
            var notSent=data(client.post("operations",Map.of("preview_id",preview.path("preview_id").asText(),"request_key","5".repeat(32))));
            assertEquals("not_sent",notSent.path("status").asText());
            assertEquals(0,fixture(Map.of("action","state")).path("ota97-device").path("upgrades").asInt());
            fixture(Map.of("action","device","device_id","ota97-device","mode","lost"));
            preview=data(client.post("preview",Map.of("device_id","ota97-device","firmware_id",ProductOtaPushHttpTest.FIRMWARE)));
            request=Map.of("preview_id",preview.path("preview_id").asText(),"request_key","6".repeat(32));
            var unknown=data(client.post("operations",request));
            assertEquals("result_unknown",unknown.path("status").asText(),unknown.toString());
            operation=unknown.path("operation_id").asText();
            assertEquals(operation,data(client.post("operations",request)).path("operation_id").asText());
        }
        fixture(Map.of("action","restart"));
        try(var restarted=start()) {
            var client=client(restarted);
            try {
                assertEquals(operation,data(client.post("operations",request)).path("operation_id").asText());
                assertEquals("result_unknown",data(client.get("operations/"+operation,ProductOtaPushHttpTest.TOKEN)).path("status").asText());
                var state=fixture(Map.of("action","state"));
                assertEquals(1,state.path("ota97-device").path("upgrades").asInt());
                assertEquals(0,state.path("ota97-unselected").path("upgrades").asInt());
                fixture(Map.of("action","device","device_id","ota97-device","mode","accepted","session_id","new-authorized-session",
                        "version","2.2.6","elf_sha256","c".repeat(64)));
                var observed=data(client.get("operations/"+operation,ProductOtaPushHttpTest.TOKEN));
                assertEquals("target_running",observed.path("observation_status").asText(),observed.toString());
                assertEquals("result_unknown",observed.path("status").asText());
                assertEquals("already_running",data(client.post("preview",Map.of("device_id","ota97-device","firmware_id",ProductOtaPushHttpTest.FIRMWARE))).path("reason").asText());
            } finally { client.cleanup(); }
        }
    }
    @Test void batchKeepsIndependentWireResultsAcrossJavaAndBridgeRestarts() throws Exception {
        fixture(Map.of("action","reset")); Map<String,String> request; String batchId;
        try(var first=start()) {
            var client=client(first); client.setup();
            client.db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-unselected')");
            var preview=data(client.post("batch-preview",Map.of("mode","all_online","device_ids",List.of(),"firmware_id",ProductOtaPushHttpTest.FIRMWARE)));
            assertEquals(2,preview.path("eligible_count").asInt());
            fixture(Map.of("action","device","device_id","ota97-device","mode","lost"));
            request=Map.of("preview_id",preview.path("preview_id").asText(),"request_key","f".repeat(32));
            var batch=data(client.post("batches",request)); batchId=batch.path("batch_id").asText();
            assertEquals("result_unknown",ProductOtaPushHttpTest.batchItem(batch,"ota97-device").path("status").asText(),batch.toString());
            assertEquals("accepted_unconfirmed",ProductOtaPushHttpTest.batchItem(batch,"ota97-unselected").path("status").asText(),batch.toString());
        }
        fixture(Map.of("action","restart"));
        try(var restarted=start()) {
            var client=client(restarted);
            try {
                assertEquals(batchId,data(client.post("batches",request)).path("batch_id").asText());
                var saved=data(client.get("batches/"+batchId,ProductOtaPushHttpTest.TOKEN));
                assertEquals("result_unknown",ProductOtaPushHttpTest.batchItem(saved,"ota97-device").path("status").asText());
                assertEquals("accepted_unconfirmed",ProductOtaPushHttpTest.batchItem(saved,"ota97-unselected").path("status").asText());
                var state=fixture(Map.of("action","state"));
                assertEquals(1,state.path("ota97-device").path("upgrades").asInt());
                assertEquals(1,state.path("ota97-unselected").path("upgrades").asInt());
                var next=data(client.post("batch-preview",Map.of("mode","all_online","device_ids",List.of(),"firmware_id",ProductOtaPushHttpTest.FIRMWARE)));
                assertEquals(0,next.path("eligible_count").asInt());
            } finally { client.cleanup(); }
        }
    }
    @Test void interruptionNeverResumesQueuedDevicesAndKeepsReservedDevicesUnknown() throws Exception {
        fixture(Map.of("action","reset"));
        for(String device:List.of("ota97-device","ota97-unselected")) fixture(Map.of("action","device","device_id",device,"mode","lost"));
        for(int i=0;i<4;i++) fixture(Map.of("action","add","device_id","ota97-extra-"+i,"mode","lost"));
        Map<String,String> request; String batchId;
        try(var first=start()) {
            var client=client(first); client.setup();
            client.db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-unselected'),('ota97-extra-0'),('ota97-extra-1'),('ota97-extra-2'),('ota97-extra-3')");
            var preview=data(client.post("batch-preview",Map.of("mode","all_online","device_ids",List.of(),"firmware_id",ProductOtaPushHttpTest.FIRMWARE)));
            assertEquals(6,preview.path("eligible_count").asInt());
            request=Map.of("preview_id",preview.path("preview_id").asText(),"request_key","0".repeat(32));
            batchId=data(client.postNow("batches",request)).path("batch_id").asText();
            int sends=0;
            for(int attempt=0;attempt<100 && sends<4;attempt++) {
                Thread.sleep(50); sends=0;
                for(var device:fixture(Map.of("action","state"))) sends+=device.path("upgrades").asInt();
            }
            assertEquals(4,sends,"Four bounded workers reserve/send; other targets remain unstarted");
        }
        fixture(Map.of("action","restart"));
        try(var restarted=start()) {
            var client=client(restarted);
            try {
                var batch=data(client.post("batches",request));
                assertEquals(batchId,batch.path("batch_id").asText());
                assertEquals("interrupted",batch.path("status").asText(),batch.toString());
                int unknown=0,notSent=0;
                for(var item:batch.path("items")) {
                    if(item.path("status").asText().equals("result_unknown")) unknown++;
                    if(item.path("status").asText().equals("not_sent")) notSent++;
                }
                assertEquals(4,unknown,batch.toString()); assertEquals(2,notSent,batch.toString());
                int sends=0; for(var device:fixture(Map.of("action","state"))) sends+=device.path("upgrades").asInt();
                assertEquals(4,sends);
            } finally { client.cleanup(); }
        }
    }
}
