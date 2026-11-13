package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.*;
import com.fasterxml.jackson.databind.*;
import java.net.*;
import java.net.http.*;
import java.util.*;
import org.junit.jupiter.api.*;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.server.LocalServerPort;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;
import com.sun.net.httpserver.HttpServer;
import java.util.concurrent.atomic.AtomicInteger;

/** Administrator HTTP is the public seam; voice is a separate protocol peer. */
@SpringBootTest(webEnvironment=SpringBootTest.WebEnvironment.RANDOM_PORT,
 properties={"server.servlet.context-path=/xiaozhi","server.forward-headers-strategy=native",
 "device-control.enabled=true","device-control.public-origin=https://speaker.example.test"})
class ProductOtaPushHttpTest {
 static final long ADMIN=970000000000001L;
 static final String TOKEN="ota97-admin-fixture";
 static final ObjectMapper JSON=new ObjectMapper();
 static HttpServer voice;
 static volatile boolean connected;
 static volatile String elf="a".repeat(64);
 static volatile String downloadUrl;
 static volatile boolean loseReply;
 static volatile boolean inspectUnavailable;
 static volatile String session="session-97";
 static volatile String hardwareCase="normal";
 static volatile long statusDelay;
 static final java.util.concurrent.atomic.AtomicLong now=new java.util.concurrent.atomic.AtomicLong();
 static final AtomicInteger inspections=new AtomicInteger();
 static volatile boolean slowPreparation;
 static volatile boolean queuedPastExpiry;
 @org.springframework.boot.test.mock.mockito.MockBean(name="deviceControlClock") java.time.Clock clock;
 static final Set<String> offlineDevices=java.util.concurrent.ConcurrentHashMap.newKeySet();
 static final String FIRMWARE="9".repeat(32);
 static final String DEVICE_CREDENTIAL="management-fixture-"+"x".repeat(32);
 static final AtomicInteger sent=new AtomicInteger();
 @DynamicPropertySource static void properties(DynamicPropertyRegistry registry) throws Exception {
  voice=HttpServer.create(new InetSocketAddress("127.0.0.1",0),0);
  voice.createContext("/internal/product-ota/v1/control",exchange->{
   var request=JSON.readTree(exchange.getRequestBody());
   if(request.path("action").asText().equals("status") && statusDelay>0) try { Thread.sleep(statusDelay); } catch(InterruptedException interrupted) { Thread.currentThread().interrupt(); }
   Object result=Map.of("status","bridge_unavailable");
   if(connected && request.path("device_id").asText().startsWith("ota97-") && !offlineDevices.contains(request.path("device_id").asText())) {
    String action=request.path("action").asText();
    if(action.equals("inspect") && slowPreparation && inspections.incrementAndGet()==5) now.addAndGet(121000);
    if(action.equals("inspect") && inspectUnavailable) { exchange.sendResponseHeaders(503,-1); exchange.close(); return; }
    if(action.equals("status")) result=Map.of("status","current","session_id",session,"permission_version",1);
    if(action.equals("inspect")) result=Map.of("status","ready","system_info",Map.of(
     "board",Map.of("type",hardwareCase.startsWith("family-") ? "esp32-p4-wifi6-touch-lcd" : "waveshare-esp32-p4-wifi6-touch-lcd-4.3"),"flash_size",33554432,
     "chip_info",Map.of("model",18,"revision",100),"application",Map.of("version","2.2.6","elf_sha256",elf),
     "ota",Map.of("label","ota_0"),"partition_table",List.of(
      partition("nvs",1,2,0x9000,0x4000),partition("otadata",1,0,0xd000,0x2000),
      partition("phy_init",1,1,0xf000,0x1000),partition("ota_0",0,16,0x20000,0x3f0000),
      partition("ota_1",0,17,0x410000,0x3f0000),partition("assets",1,130,0x800000,0x800000))));
    if(action.equals("inspect") && !hardwareCase.equals("normal")) {
     var changed=(com.fasterxml.jackson.databind.node.ObjectNode)JSON.valueToTree(result);
     var info=(com.fasterxml.jackson.databind.node.ObjectNode)changed.path("system_info");
     if(hardwareCase.equals("missing")) info.remove("chip_info");
     if(hardwareCase.equals("small-slot") || hardwareCase.equals("family-small-slot")) ((com.fasterxml.jackson.databind.node.ObjectNode)info.path("partition_table").get(4)).put("size",2048);
     if(hardwareCase.equals("unknown-board")) ((com.fasterxml.jackson.databind.node.ObjectNode)info.path("board")).put("type","unrecognized-p4");
     if(hardwareCase.equals("family-wrong-chip")) ((com.fasterxml.jackson.databind.node.ObjectNode)info.path("chip_info")).put("model",9);
     if(hardwareCase.equals("family-wrong-revision")) ((com.fasterxml.jackson.databind.node.ObjectNode)info.path("chip_info")).put("revision",400);
     result=changed;
    }
    if(action.equals("dispatch")) {
     if(sent.incrementAndGet()==1 && queuedPastExpiry) now.addAndGet(2000); downloadUrl=request.path("url").asText();
     if(loseReply) { exchange.sendResponseHeaders(503,-1); exchange.close(); return; }
     result=Map.of("status","accepted_unconfirmed");
    }
    if(action.equals("reconcile")) result=elf.equals("c".repeat(64)) && !session.equals("session-97")
      ? Map.of("status","result_unknown","observation_status","target_running","observed_at_ms",System.currentTimeMillis()) : Map.of("status","result_unknown");
   }
   if(offlineDevices.contains(request.path("device_id").asText())) result=Map.of("status","offline");
   if(connected && !request.path("device_id").asText().startsWith("ota97-")) result=Map.of("status","offline");
   byte[] body=JSON.writeValueAsBytes(result); exchange.sendResponseHeaders(200,body.length);
   exchange.getResponseBody().write(body); exchange.close();
  }); voice.start();
  registry.add("product.ota.bridge-url",()->"http://127.0.0.1:"+voice.getAddress().getPort()+"/internal/product-ota/v1/control");
  registry.add("product.ota.bridge-key",()->"fixture-maintenance-key-97-123456789");
  registry.add("product.ota.download-origin",()->"https://firmware.example.test");
  registry.add("product.ota.download-key",()->"fixture-download-key-97-1234567890");
 }
 static Map<String,Object> partition(String label,int type,int subtype,int address,int size) {
  return Map.of("label",label,"type",type,"subtype",subtype,"address",address,"size",size);
 }
 @AfterAll static void stop() { if(voice!=null) voice.stop(0); }
 @LocalServerPort int port;
 @Autowired JdbcTemplate db;
 final HttpClient http=HttpClient.newHttpClient();
 @BeforeEach void setup() {
  cleanup();
  now.set(System.currentTimeMillis()); slowPreparation=false; queuedPastExpiry=false; inspections.set(0);
  if(clock!=null) org.mockito.Mockito.when(clock.millis()).thenAnswer(call->now.get());
  connected=false; sent.set(0); elf="a".repeat(64); downloadUrl=null; loseReply=false; inspectUnavailable=false; session="session-97"; hardwareCase="normal"; statusDelay=0; offlineDevices.clear();
  db.update("INSERT INTO sys_user(id,username,super_admin,status) VALUES (?, 'ota97-admin',1,1)",ADMIN);
  db.update("INSERT INTO sys_user_token(id,user_id,token,expire_date) VALUES (?,?,?,DATE_ADD(CURRENT_TIMESTAMP,INTERVAL 1 DAY))",ADMIN,ADMIN,TOKEN);
  db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-device')");
  db.update("INSERT INTO dc_ota_firmware(firmware_id,github_release_id,github_asset_id,release_tag,prerelease,source_commit,source_url,manifest_sha256,sha256,elf_sha256,app_version,board_type,flash_size,min_revision,max_revision,partition_layout,partition_table_sha256,app_partition_bytes,image_size,image,verification_status,operator_id,created_at_ms) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
   FIRMWARE,9701,97010,"test",false,"a".repeat(40),"https://example.test","b".repeat(64),ProductOtaCatalog.digest(new byte[]{1,2,3}),"c".repeat(64),"2.2.6","waveshare-esp32-p4-wifi6-touch-lcd-4.3",16777216,100,399,"partitions/v2/16m.csv","d".repeat(64),4128768,3,new byte[]{1,2,3},"verified",ADMIN,1);
 }
 @AfterEach void cleanup() {
  db.update("DELETE FROM dc_ota_batch WHERE operator_id=?",ADMIN);
  db.update("DELETE FROM dc_ota_operation WHERE operator_id=?",ADMIN);
  db.update("DELETE FROM dc_ota_preview WHERE operator_id=?",ADMIN);
  db.update("DELETE FROM dc_ota_firmware WHERE operator_id=?",ADMIN);
  db.update("DELETE FROM dc_delivery_operation WHERE device_id LIKE 'ota97-%'");
  db.update("DELETE FROM dc_device_presence WHERE device_id LIKE 'ota97-%'");
  db.update("DELETE FROM dc_device_boot WHERE device_id LIKE 'ota97-%'");
  db.update("DELETE FROM dc_device WHERE device_id LIKE 'ota97-%'");
  db.update("DELETE FROM sys_user_token WHERE user_id=?",ADMIN);
  db.update("DELETE FROM sys_user WHERE id=?",ADMIN);
 }
 HttpResponse<String> post(String path,Object body) throws Exception {
  var response=postNow(path,body);
  if(response.statusCode()==200 && Set.of("batch-preview","batches").contains(path)) {
   var value=JSON.readTree(response.body()).path("data");
   String lookup=path.equals("batch-preview") ? "batch-previews/"+value.path("preview_id").asText() : "batches/"+value.path("batch_id").asText();
   long deadline=System.nanoTime()+java.time.Duration.ofSeconds(100).toNanos();
   while(Set.of("preparing","started").contains(value.path("status").asText())) {
    if(System.nanoTime()>deadline) fail("Async OTA request did not settle");
    Thread.sleep(50); response=get(lookup,TOKEN); value=JSON.readTree(response.body()).path("data");
   }
  }
  return response;
 }
 HttpResponse<String> postNow(String path,Object body) throws Exception {
  return http.send(HttpRequest.newBuilder(URI.create("http://localhost:"+port+"/xiaozhi/admin/product-ota/"+path))
   .header("Authorization","Bearer "+TOKEN).header("Content-Type","application/json")
   .POST(HttpRequest.BodyPublishers.ofString(JSON.writeValueAsString(body))).build(),HttpResponse.BodyHandlers.ofString());
 }
 @Test void previewsActualBoardLayoutAndDistinctElfWithoutExposingSessionOrDownload() throws Exception {
  connected=true;
  var response=post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE));
  assertEquals(200,response.statusCode(),response.body());
  var preview=JSON.readTree(response.body()).path("data");
  assertTrue(preview.path("eligible").asBoolean(),response.body());
  assertEquals("2.2.6",preview.path("current_version").asText());
  assertFalse(response.body().contains("session-97")); assertFalse(response.body().contains("signature"));
  elf="c".repeat(64);
  var same=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data");
  assertFalse(same.path("eligible").asBoolean()); assertEquals("already_running",same.path("reason").asText());
  assertEquals(0,sent.get());
 }
 void delivery(String board,boolean complete) {
  db.update("INSERT INTO dc_delivery_operation(operation_id,operator_id,device_id,hardware_id,network_hardware_id,board_type,credential_hash,credential_cipher,status,created_at_ms,device_verified_at_ms,write_operation_id,completed_at_ms) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
    "d".repeat(32),ADMIN,"ota97-device","020000009701","020000009702",board,"e".repeat(64),"fixture-cipher",
    complete ? "delivered" : "pending",1,complete ? 2L : null,complete ? "f".repeat(32) : null,complete ? 3L : null);
 }
 @Test void familyNameWithCompletedPreciseDeliveryCanPreviewAndConfirm() throws Exception {
  connected=true; hardwareCase="family-normal";
  delivery("waveshare-esp32-p4-wifi6-touch-lcd-4.3",true);
  var response=post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE));
  assertEquals(200,response.statusCode(),response.body());
  var preview=JSON.readTree(response.body()).path("data");
  assertTrue(preview.path("eligible").asBoolean(),response.body());
  assertEquals("eligible",preview.path("reason").asText());
  assertEquals(0,sent.get());
  var confirmed=post("operations",Map.of("preview_id",preview.path("preview_id").asText(),"request_key","6".repeat(32)));
  assertEquals(200,confirmed.statusCode(),confirmed.body());
  assertEquals("accepted_unconfirmed",JSON.readTree(confirmed.body()).path("data").path("status").asText());
  assertEquals(1,sent.get());
 }
 @Test void genericNameWithoutCompletedPreciseDeliveryNeverBecomesEligible() throws Exception {
  connected=true; hardwareCase="family-normal";
  for(String evidence:List.of("missing","pending","other-screen","unknown")) {
   db.update("DELETE FROM dc_delivery_operation WHERE device_id='ota97-device'");
   if(!evidence.equals("missing")) delivery(evidence.equals("pending") ? "waveshare-esp32-p4-wifi6-touch-lcd-4.3"
     : evidence.equals("other-screen") ? "waveshare-esp32-p4-wifi6-touch-lcd-7" : "unknown-board",!evidence.equals("pending"));
   var response=post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE));
   assertEquals(200,response.statusCode(),response.body());
   var preview=JSON.readTree(response.body()).path("data");
   assertFalse(preview.path("eligible").asBoolean(),evidence+": "+response.body());
   assertEquals("incompatible_board",preview.path("reason").asText(),evidence);
   assertEquals("",preview.path("preview_id").asText());
  }
  assertEquals(0,sent.get());
  assertEquals(0,JSON.readTree(get("operations",TOKEN).body()).path("data").size());
 }
 @Test void exactRuntimeNameCannotOverrideAConflictingDelivery() throws Exception {
  connected=true;
  delivery("waveshare-esp32-p4-wifi6-touch-lcd-7",true);
  var response=post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE));
  var preview=JSON.readTree(response.body()).path("data");
  assertFalse(preview.path("eligible").asBoolean(),response.body());
  assertEquals("incompatible_board",preview.path("reason").asText());
  assertEquals(0,sent.get());
 }
 @Test void preciseDeliveryDoesNotOverrideUnknownRuntimeOrOtherHardwareChecks() throws Exception {
  connected=true; delivery("waveshare-esp32-p4-wifi6-touch-lcd-4.3",true);
  for(var scenario:Map.of("unknown-board","incompatible_board","family-wrong-chip","incompatible_board",
    "family-wrong-revision","incompatible_revision","family-small-slot","incompatible_layout").entrySet()) {
   hardwareCase=scenario.getKey();
   var response=post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE));
   var preview=JSON.readTree(response.body()).path("data");
   assertFalse(preview.path("eligible").asBoolean(),response.body());
   assertEquals(scenario.getValue(),preview.path("reason").asText(),scenario.getKey());
  }
  assertEquals(0,sent.get());
  assertEquals(0,JSON.readTree(get("operations",TOKEN).body()).path("data").size());
 }
 @ParameterizedTest
 @ValueSource(strings={"other-screen","pending","removed"})
 void confirmationRechecksCurrentDeliveryEvidenceBeforeDispatch(String change) throws Exception {
  connected=true; hardwareCase="family-normal";
  delivery("waveshare-esp32-p4-wifi6-touch-lcd-4.3",true);
  var preview=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data");
  assertTrue(preview.path("eligible").asBoolean(),preview.toString());
  if(change.equals("other-screen")) db.update("UPDATE dc_delivery_operation SET board_type='waveshare-esp32-p4-wifi6-touch-lcd-7' WHERE device_id='ota97-device'");
  else if(change.equals("pending")) db.update("UPDATE dc_delivery_operation SET status='pending',device_verified_at_ms=NULL,write_operation_id=NULL,completed_at_ms=NULL WHERE device_id='ota97-device'");
  else db.update("DELETE FROM dc_delivery_operation WHERE device_id='ota97-device'");
  var response=post("operations",Map.of("preview_id",preview.path("preview_id").asText(),"request_key","6".repeat(32)));
  assertEquals(200,response.statusCode(),response.body());
  var operation=JSON.readTree(response.body()).path("data");
  assertEquals("not_sent",operation.path("status").asText(),response.body());
  assertEquals("incompatible_board",operation.path("reason").asText());
  assertEquals(0,sent.get());
 }
 @Test void slowPreparationLeavesAFullConfirmationWindow() throws Exception {
  connected=true; slowPreparation=true;
  db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-b'),('ota97-c'),('ota97-d'),('ota97-e')");
  var preview=JSON.readTree(post("batch-preview",Map.of("mode","selected","device_ids",List.of("ota97-device","ota97-b","ota97-c","ota97-d","ota97-e"),"firmware_id",FIRMWARE)).body()).path("data");
  assertEquals(5,preview.path("eligible_count").asInt(),preview.toString());
  assertEquals(now.get()+120000,preview.path("expires_at_ms").asLong(),"Confirmation window starts when preparation finishes");
  var response=post("batches",Map.of("preview_id",preview.path("preview_id").asText(),"request_key","f".repeat(32)));
  assertEquals(200,response.statusCode(),response.body());
  assertEquals(5,sent.get());
 }
 @Test void acceptedBatchDoesNotExpireWhileTargetsWaitForAWorker() throws Exception {
  connected=true;
  db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-b'),('ota97-c'),('ota97-d'),('ota97-e')");
  var preview=JSON.readTree(post("batch-preview",Map.of("mode","selected","device_ids",List.of("ota97-device","ota97-b","ota97-c","ota97-d","ota97-e"),"firmware_id",FIRMWARE)).body()).path("data");
  now.set(preview.path("expires_at_ms").asLong()-1000); queuedPastExpiry=true;
  var request=Map.of("preview_id",preview.path("preview_id").asText(),"request_key","f".repeat(32));
  var response=post("batches",request);
  assertEquals(200,response.statusCode(),response.body());
  var batch=JSON.readTree(response.body()).path("data");
  for(var item:batch.path("items")) assertEquals("accepted_unconfirmed",item.path("status").asText(),batch.toString());
  assertEquals(5,sent.get());
  post("batches",request); assertEquals(5,sent.get());
 }
 @Test void selectedBatchPreviewsOnlyChosenDevicesAndConfirmNeverReplays() throws Exception {
  connected=true;
  db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-second'),('ota97-unselected')");
  var response=post("batch-preview",Map.of("mode","selected","device_ids",List.of("ota97-device","ota97-second"),"firmware_id",FIRMWARE));
  assertEquals(200,response.statusCode(),response.body());
  var preview=JSON.readTree(response.body()).path("data");
  assertEquals(2,preview.path("eligible_count").asInt());
  assertEquals(2,preview.path("items").size());
  assertEquals(0,sent.get());
  var request=Map.of("preview_id",preview.path("preview_id").asText(),"request_key","b".repeat(32));
  var confirmed=post("batches",request);
  assertEquals(200,confirmed.statusCode(),confirmed.body());
  var batch=JSON.readTree(confirmed.body()).path("data");
  assertEquals(2,batch.path("items").size());
  for(var item:batch.path("items")) assertEquals("accepted_unconfirmed",item.path("status").asText());
  assertEquals(2,sent.get());
  assertEquals(batch.path("batch_id"),JSON.readTree(post("batches",request).body()).path("data").path("batch_id"));
  assertEquals(2,sent.get());
  assertEquals(200,get("batches/"+batch.path("batch_id").asText(),TOKEN).statusCode());
 }
 @Test void batchPreparationAndConfirmationReturnPollableReceiptsWithoutHoldingTheHttpConnection() throws Exception {
  connected=true;
  var response=postNow("batch-preview",Map.of("mode","selected","device_ids",List.of("ota97-device"),"firmware_id",FIRMWARE));
  assertEquals(200,response.statusCode(),response.body());
  var pending=JSON.readTree(response.body()).path("data");
  assertEquals("preparing",pending.path("status").asText());
  String preview=pending.path("preview_id").asText();
  JsonNode ready=pending;
  for(int attempt=0;attempt<100 && ready.path("status").asText().equals("preparing");attempt++) {
   Thread.sleep(50); ready=JSON.readTree(get("batch-previews/"+preview,TOKEN).body()).path("data");
  }
  assertEquals("ready",ready.path("status").asText());
  var request=Map.of("preview_id",preview,"request_key","a".repeat(32));
  var receipt=JSON.readTree(postNow("batches",request).body()).path("data");
  assertEquals("started",receipt.path("status").asText());
  var settled=JSON.readTree(post("batches",request).body()).path("data");
  assertEquals("finished",settled.path("status").asText()); assertEquals(1,sent.get());
 }
 @Test void selectedIdsAcrossPagesUseTheBoundedBatchBodyRatherThanTheSingleDeviceBodyLimit() throws Exception {
  var selected=new ArrayList<String>();
  for(int i=0;i<40;i++) {
   String device="ota97-"+"x".repeat(110)+i; selected.add(device);
   db.update("INSERT INTO dc_device(device_id) VALUES (?)",device);
  }
  var response=post("batch-preview",Map.of("mode","selected","device_ids",selected,"firmware_id",FIRMWARE));
  assertEquals(200,response.statusCode(),response.body());
  var preview=JSON.readTree(response.body()).path("data");
  assertEquals("ready",preview.path("status").asText()); assertEquals(40,preview.path("items").size());
  assertEquals(0,sent.get());
 }
 @Test void unavailableInventoryHasAShortBoundInsteadOfOneLongTimeoutPerDevice() throws Exception {
  db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-second'),('ota97-third'),('ota97-fourth')");
  db.update("UPDATE dc_device SET credential_hash=? WHERE device_id='ota97-device'",ProductOtaCatalog.digest(DEVICE_CREDENTIAL.getBytes(java.nio.charset.StandardCharsets.UTF_8)));
  assertEquals(200,http.send(HttpRequest.newBuilder(URI.create("http://localhost:"+port+"/xiaozhi/device-control/v1/state"))
   .header("Authorization","Bearer "+DEVICE_CREDENTIAL).header("Device-Id","ota97-device").header("X-Forwarded-Proto","https").GET().build(),HttpResponse.BodyHandlers.ofString()).statusCode());
  statusDelay=3000;
  long start=System.nanoTime();
  var response=get("devices?limit=100",TOKEN);
  assertEquals(200,response.statusCode(),response.body());
  assertTrue((System.nanoTime()-start)/1_000_000<5000,"Unavailable status must fail closed within five seconds for this page");
  for(var device:JSON.readTree(response.body()).path("data").path("items"))
   assertTrue(Set.of("bridge_unavailable","online_requires_wake").contains(device.path("status").asText()),device.toString());
 }
 @Test void allOnlineSpansPagesButFreezesExclusionsAndDoesNotAddLaterRegistrationsOrReconnects() throws Exception {
  connected=true;
  db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-second'),('ota97-offline')");
  offlineDevices.add("ota97-offline");
  assertEquals(1,JSON.readTree(get("devices?limit=1",TOKEN).body()).path("data").path("items").size());
  var preview=JSON.readTree(post("batch-preview",Map.of("mode","all_online","device_ids",List.of(),"firmware_id",FIRMWARE)).body()).path("data");
  assertEquals(2,preview.path("eligible_count").asInt(),preview.toString());
  assertEquals("offline",batchItem(preview,"ota97-offline").path("reason").asText());
  db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-later')");
  offlineDevices.remove("ota97-offline"); offlineDevices.add("ota97-second");
  var request=Map.of("preview_id",preview.path("preview_id").asText(),"request_key","c".repeat(32));
  var batch=JSON.readTree(post("batches",request).body()).path("data");
  assertEquals("accepted_unconfirmed",batchItem(batch,"ota97-device").path("status").asText(),batch.toString());
  assertEquals("not_sent",batchItem(batch,"ota97-second").path("status").asText());
  assertEquals("not_sent",batchItem(batch,"ota97-offline").path("status").asText());
  assertTrue(batchItem(batch,"ota97-later").isMissingNode()); assertEquals(1,sent.get());
  offlineDevices.clear();
  post("batches",request); assertEquals(1,sent.get());
 }
 static JsonNode batchItem(JsonNode batch,String device) {
  for(var item:batch.path("items")) if(device.equals(item.path("device_id").asText())) return item;
  return com.fasterxml.jackson.databind.node.MissingNode.getInstance();
 }
 @Test void overlappingBatchesAndUnknownRepliesNeverResendADevice() throws Exception {
  connected=true; loseReply=true;
  db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-second')");
  var selection=Map.of("mode","selected","device_ids",List.of("ota97-device","ota97-second"),"firmware_id",FIRMWARE);
  String one=JSON.readTree(post("batch-preview",selection).body()).path("data").path("preview_id").asText();
  String two=JSON.readTree(post("batch-preview",selection).body()).path("data").path("preview_id").asText();
  var gate=new java.util.concurrent.CountDownLatch(1);
  var left=java.util.concurrent.CompletableFuture.supplyAsync(()->batchRace(gate,one,"d".repeat(32)));
  var right=java.util.concurrent.CompletableFuture.supplyAsync(()->batchRace(gate,two,"e".repeat(32)));
  gate.countDown();
  assertEquals(200,left.get().statusCode()); assertEquals(200,right.get().statusCode());
  assertEquals(2,sent.get());
  for(var operation:JSON.readTree(get("operations",TOKEN).body()).path("data")) assertEquals("result_unknown",operation.path("status").asText());
  post("batches",Map.of("preview_id",one,"request_key","d".repeat(32)));
  post("batches",Map.of("preview_id",two,"request_key","e".repeat(32)));
  assertEquals(2,sent.get());
  assertEquals(409,post("batches",Map.of("preview_id",two,"request_key","d".repeat(32))).statusCode());
 }
 HttpResponse<String> batchRace(java.util.concurrent.CountDownLatch gate,String preview,String key) {
  try { gate.await(); return post("batches",Map.of("preview_id",preview,"request_key",key)); }
  catch(Exception failed) { throw new RuntimeException(failed); }
 }
 @Test void explicitConfirmationReservesOneDurableOperationAndDuplicateClicksNeverResend() throws Exception {
  connected=true;
  var preview=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data");
  var request=Map.of("preview_id",preview.path("preview_id").asText(),"request_key","1".repeat(32));
  var response=post("operations",request);
  assertEquals(200,response.statusCode(),response.body());
  var operation=JSON.readTree(response.body()).path("data");
  assertEquals("accepted_unconfirmed",operation.path("status").asText(),response.body());
  assertEquals("unobserved",operation.path("observation_status").asText());
  assertEquals("ota97-device",operation.path("device_id").asText());
  var repeated=JSON.readTree(post("operations",request).body()).path("data");
  assertEquals(operation.path("operation_id"),repeated.path("operation_id"));
  assertEquals(1,sent.get());
  var saved=JSON.readTree(get("operations",TOKEN).body()).path("data");
  assertTrue(saved.toString().contains(operation.path("operation_id").asText()));
  assertFalse(saved.toString().contains("signature")); assertFalse(saved.toString().contains("session-97"));
  var blocked=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data");
  assertFalse(blocked.path("eligible").asBoolean()); assertEquals("operation_pending",blocked.path("reason").asText());
 }
 HttpResponse<byte[]> download(String path,boolean secure) throws Exception {
  var request=HttpRequest.newBuilder(URI.create("http://localhost:"+port+path));
  if(secure) request.header("X-Forwarded-Proto","https");
  return http.send(request.GET().build(),HttpResponse.BodyHandlers.ofByteArray());
 }
 @Test void fixedBytesRequireOperationScopedShortLivedHttpsCapability() throws Exception {
  connected=true;
  var preview=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data");
  post("operations",Map.of("preview_id",preview.path("preview_id").asText(),"request_key","2".repeat(32)));
  assertNotNull(downloadUrl);
  var uri=URI.create(downloadUrl); String path=uri.getRawPath()+"?"+uri.getRawQuery();
  assertArrayEquals(new byte[]{1,2,3},download(path,true).body());
  assertEquals(403,download(path,false).statusCode());
  assertEquals(404,download(uri.getRawPath(),true).statusCode());
  assertEquals(404,download(path+"&expires=1",true).statusCode());
  assertEquals(404,download(path.replace("signature=","signature=0"),true).statusCode());
  db.update("UPDATE dc_ota_firmware SET image=? WHERE firmware_id=?",new byte[]{3,2,1},FIRMWARE);
  assertEquals(404,download(path,true).statusCode());
 }
 @Test void lostReplyNeverResendsAndNewSessionEvidenceDoesNotRewriteUnknownTransport() throws Exception {
  connected=true; loseReply=true;
  var preview=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data");
  var request=Map.of("preview_id",preview.path("preview_id").asText(),"request_key","3".repeat(32));
  var operation=JSON.readTree(post("operations",request).body()).path("data");
  assertEquals("result_unknown",operation.path("status").asText());
  assertEquals(operation.path("operation_id"),JSON.readTree(post("operations",request).body()).path("data").path("operation_id"));
  elf="c".repeat(64);
  String path="operations/"+operation.path("operation_id").asText();
  assertEquals("unobserved",JSON.readTree(get(path,TOKEN).body()).path("data").path("observation_status").asText());
  session="session-after-upgrade";
  var observed=JSON.readTree(get(path,TOKEN).body()).path("data");
  assertEquals("target_running",observed.path("observation_status").asText());
  assertEquals("result_unknown",observed.path("status").asText()); assertEquals(1,sent.get());
  var after=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data");
  assertEquals("already_running",after.path("reason").asText());
 }
 @Test void failureBeforeDispatchIsDurablyNotSentAndDoesNotWaitForReconnect() throws Exception {
  connected=true;
  var preview=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data");
  inspectUnavailable=true;
  var request=Map.of("preview_id",preview.path("preview_id").asText(),"request_key","4".repeat(32));
  var response=post("operations",request);
  var operation=JSON.readTree(response.body()).path("data");
  assertEquals("not_sent",operation.path("status").asText(),response.body());
  inspectUnavailable=false;
  assertEquals("not_sent",JSON.readTree(post("operations",request).body()).path("data").path("status").asText());
  assertEquals(0,sent.get());
  assertTrue(JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data").path("eligible").asBoolean());
 }
 @Test void operatorAuthorizationAndStrictInputsCannotBeBypassedWithClientTargets() throws Exception {
  connected=true;
  assertEquals(400,post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE,"url","https://attacker.test/image.bin")).statusCode());
  assertEquals(404,post("preview",Map.of("device_id","not-registered","firmware_id",FIRMWARE)).statusCode());
  assertEquals(400,get("devices?limit=101",TOKEN).statusCode());
  for(String raw:List.of("{\"device_id\":\"ota97-device\",\"device_id\":\"other\",\"firmware_id\":\""+FIRMWARE+"\"}"," ".repeat(4097))) {
   var response=http.send(HttpRequest.newBuilder(URI.create("http://localhost:"+port+"/xiaozhi/admin/product-ota/preview"))
    .header("Authorization","Bearer "+TOKEN).header("Content-Type","application/json")
    .POST(HttpRequest.BodyPublishers.ofString(raw)).build(),HttpResponse.BodyHandlers.ofString());
   assertEquals(400,response.statusCode());
  }
  for(String token:List.of("device-fixture","customer-fixture","delivery-tool-fixture")) assertEquals(401,get("operations",token).statusCode());
  db.update("UPDATE sys_user SET super_admin=0 WHERE id=?",ADMIN);
  assertEquals(403,get("devices",TOKEN).statusCode());
  assertEquals(403,post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).statusCode());
  assertEquals(0,sent.get());
 }
 @Test void concurrentConfirmationsForOneDeviceHaveOnePersistentWinner() throws Exception {
  connected=true;
  var one=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data").path("preview_id").asText();
  var two=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data").path("preview_id").asText();
  var gate=new java.util.concurrent.CountDownLatch(1);
  var left=java.util.concurrent.CompletableFuture.supplyAsync(()->race(gate,one,"7".repeat(32)));
  var right=java.util.concurrent.CompletableFuture.supplyAsync(()->race(gate,two,"8".repeat(32)));
  gate.countDown();
  assertEquals(Set.of(200,409),Set.of(left.get().statusCode(),right.get().statusCode()));
  assertEquals(1,sent.get());
  assertEquals(1,JSON.readTree(get("operations",TOKEN).body()).path("data").size());
 }
 HttpResponse<String> race(java.util.concurrent.CountDownLatch gate,String preview,String key) {
  try { gate.await(); return post("operations",Map.of("preview_id",preview,"request_key",key)); }
  catch(Exception failed) { throw new RuntimeException(failed); }
 }
 @Test void unknownHardwareAndInsufficientInactiveSlotNeverBecomeEligible() throws Exception {
  connected=true;
  hardwareCase="missing";
  var missing=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data");
  assertFalse(missing.path("eligible").asBoolean()); assertEquals("hardware_unknown",missing.path("reason").asText());
  hardwareCase="small-slot";
  var small=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data");
  assertFalse(small.path("eligible").asBoolean()); assertEquals("incompatible_layout",small.path("reason").asText());
  assertEquals(0,sent.get());
 }
 HttpResponse<String> get(String path,String token) throws Exception {
  var req=HttpRequest.newBuilder(URI.create("http://localhost:"+port+"/xiaozhi/admin/product-ota/"+path));
  if(token!=null) req.header("Authorization","Bearer "+token);
  return http.send(req.GET().build(),HttpResponse.BodyHandlers.ofString());
 }
 @Test void administratorSeesProductInventoryAndUnavailableBridgeCannotPromiseOnline() throws Exception {
  var response=get("devices?page=1&limit=50",TOKEN);
  assertEquals(200,response.statusCode(),response.body());
  var first=JSON.readTree(response.body()).path("data");
  JsonNode device=null;
  for(int page=1;page<=((first.path("total").asInt()+49)/50);page++) {
   var rows=page==1 ? first.path("items") : JSON.readTree(get("devices?page="+page+"&limit=50",TOKEN).body()).path("data").path("items");
   for(var row:rows) if(row.path("device_id").asText().equals("ota97-device")) device=row;
  }
  assertNotNull(device,"Product inventory must include the fixture across pages");
  assertEquals("bridge_unavailable",device.path("status").asText());
  assertEquals(401,get("devices",null).statusCode());
 }
 @Test void authenticatedBackgroundPollMakesUnboundDeviceOnlineWithoutVoice() throws Exception {
  connected=true; offlineDevices.add("ota97-device");
  db.update("UPDATE dc_device SET credential_hash=? WHERE device_id='ota97-device'",ProductOtaCatalog.digest(DEVICE_CREDENTIAL.getBytes(java.nio.charset.StandardCharsets.UTF_8)));
  var response=http.send(HttpRequest.newBuilder(URI.create("http://localhost:"+port+"/xiaozhi/device-control/v1/state"))
   .header("Authorization","Bearer "+DEVICE_CREDENTIAL).header("Device-Id","ota97-device")
   .header("X-Forwarded-Proto","https").GET().build(),HttpResponse.BodyHandlers.ofString());
  assertEquals(200,response.statusCode(),response.body());
  var inventory=JSON.readTree(get("devices?limit=100",TOKEN).body()).path("data");
  JsonNode device=batchItem(inventory,"ota97-device");
  assertTrue(device.path("online").asBoolean(),inventory.toString());
  assertEquals("online_requires_wake",device.path("status").asText());
  now.addAndGet(90001);
  assertFalse(batchItem(JSON.readTree(get("devices?limit=100",TOKEN).body()).path("data"),"ota97-device").path("online").asBoolean());
 }
 static Map<String,Object> maintenanceReport(String boot,boolean idle,Object operation) {
  var report=new LinkedHashMap<String,Object>();
  report.put("boot_id",boot); report.put("ota_capable",true); report.put("idle",idle); report.put("operation",operation);
  report.put("system_info",Map.of("board",Map.of("type","waveshare-esp32-p4-wifi6-touch-lcd-4.3"),"flash_size",16777216,
   "chip_info",Map.of("model",18,"revision",302),"application",Map.of("version","2.2.6","elf_sha256",elf),
   "ota",Map.of("label","ota_0","state","valid"),"partition_table",List.of(
    partition("nvs",1,2,0x9000,0x4000),partition("otadata",1,0,0xd000,0x2000),
    partition("phy_init",1,1,0xf000,0x1000),partition("ota_0",0,16,0x20000,0x3f0000),
    partition("ota_1",0,17,0x410000,0x3f0000),partition("assets",1,130,0x800000,0x800000))));
  return report;
 }
 HttpResponse<String> maintenance(Object report) throws Exception {
  return http.send(HttpRequest.newBuilder(URI.create("http://localhost:"+port+"/xiaozhi/device-control/v1/maintenance"))
   .header("Authorization","Bearer "+DEVICE_CREDENTIAL).header("Device-Id","ota97-device")
   .header("X-Forwarded-Proto","https").header("Content-Type","application/json")
   .POST(HttpRequest.BodyPublishers.ofString(JSON.writeValueAsString(report))).build(),HttpResponse.BodyHandlers.ofString());
 }
 @Test void managementOtaWaitsForIdleAndRequiresDurableAcceptance() throws Exception {
  db.update("UPDATE dc_device SET credential_hash=? WHERE device_id='ota97-device'",ProductOtaCatalog.digest(DEVICE_CREDENTIAL.getBytes(java.nio.charset.StandardCharsets.UTF_8)));
  String boot="1".repeat(32);
  var contact=maintenance(maintenanceReport(boot,false,null));
  assertEquals(200,contact.statusCode(),contact.body());
  var inventory=batchItem(JSON.readTree(get("devices?limit=100",TOKEN).body()).path("data"),"ota97-device");
  assertTrue(inventory.path("online").asBoolean()); assertEquals("management",inventory.path("ota_transport").asText());
  var preview=JSON.readTree(post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE)).body()).path("data");
  assertTrue(preview.path("eligible").asBoolean(),preview.toString());
  var request=Map.of("preview_id",preview.path("preview_id").asText(),"request_key","7".repeat(32));
  var operation=JSON.readTree(post("operations",request).body()).path("data");
  assertEquals("waiting_idle",operation.path("status").asText(),operation.toString());
  assertEquals("management",operation.path("ota_transport").asText());
  String id=operation.path("operation_id").asText();
  assertTrue(JSON.readTree(maintenance(maintenanceReport(boot,false,null)).body()).path("data").path("command").isNull());
  var offered=JSON.readTree(maintenance(maintenanceReport(boot,true,null)).body()).path("data").path("command");
  assertEquals(id,offered.path("operation_id").asText(),offered.toString());
  assertEquals("c".repeat(64),offered.path("target_elf_sha256").asText());
  assertEquals(3,offered.path("image_size").asInt());
  assertEquals("command_offered",JSON.readTree(get("operations/"+id,TOKEN).body()).path("data").path("status").asText());
  var accepted=Map.of("operation_id",id,"origin_boot_id",boot,"status","accepted");
  var ack=JSON.readTree(maintenance(maintenanceReport(boot,true,accepted)).body()).path("data");
  assertEquals(id,ack.path("operation_ack").path("operation_id").asText());
  assertEquals("accepted",ack.path("operation_ack").path("status").asText());
  assertTrue(ack.path("operation_ack").path("expires_in_ms").asLong()>0);
  assertTrue(ack.path("command").isNull());
  assertEquals("accepted_unconfirmed",JSON.readTree(get("operations/"+id,TOKEN).body()).path("data").path("status").asText());
  assertEquals(id,JSON.readTree(post("operations",request).body()).path("data").path("operation_id").asText());
  assertEquals(0,sent.get());
 }
 void admitManagement(String boot) throws Exception {
  db.update("UPDATE dc_device SET credential_hash=? WHERE device_id='ota97-device'",ProductOtaCatalog.digest(DEVICE_CREDENTIAL.getBytes(java.nio.charset.StandardCharsets.UTF_8)));
  var response=maintenance(maintenanceReport(boot,true,null));
  assertEquals(200,response.statusCode(),response.body());
 }
 JsonNode managementPreview() throws Exception {
  var response=post("preview",Map.of("device_id","ota97-device","firmware_id",FIRMWARE));
  assertEquals(200,response.statusCode(),response.body());
  return JSON.readTree(response.body()).path("data");
 }
 JsonNode managementOperation() throws Exception {
  var preview=managementPreview(); assertTrue(preview.path("eligible").asBoolean(),preview.toString());
  var response=post("operations",Map.of("preview_id",preview.path("preview_id").asText(),"request_key",UUID.randomUUID().toString().replace("-","")));
  assertEquals(200,response.statusCode(),response.body()); return JSON.readTree(response.body()).path("data");
 }
 JsonNode operationData(String id) throws Exception { return JSON.readTree(get("operations/"+id,TOKEN).body()).path("data"); }
 Object receipt(String id,String boot,String status) { return Map.of("operation_id",id,"origin_boot_id",boot,"status",status); }
 HttpResponse<byte[]> downloadCommand(JsonNode command) throws Exception {
  var url=URI.create(command.path("url").asText());
  return http.send(HttpRequest.newBuilder(URI.create("http://localhost:"+port+url.getRawPath()+"?"+url.getRawQuery()))
   .header("X-Forwarded-Proto","https").GET().build(),HttpResponse.BodyHandlers.ofByteArray());
 }
 @Test void managementDownloadRequiresAcceptanceAndCurrentDeviceCredential() throws Exception {
  String boot="1".repeat(32); admitManagement(boot); String id=managementOperation().path("operation_id").asText();
  var command=JSON.readTree(maintenance(maintenanceReport(boot,true,null)).body()).path("data").path("command");
  assertEquals(404,downloadCommand(command).statusCode());
  assertEquals(200,maintenance(maintenanceReport(boot,true,receipt(id,boot,"accepted"))).statusCode());
  assertArrayEquals(new byte[]{1,2,3},downloadCommand(command).body());
  db.update("UPDATE dc_device SET credential_hash=? WHERE device_id='ota97-device'","b".repeat(64));
  assertEquals(404,downloadCommand(command).statusCode(),"A previously issued management download must fail after credential rotation");
 }
 @Test void managementInstallNeedsNewBootTargetElfAndValidSlotAndPreservesTransportReceipt() throws Exception {
  String boot="1".repeat(32); admitManagement(boot); String id=managementOperation().path("operation_id").asText();
  maintenance(maintenanceReport(boot,true,null));
  maintenance(maintenanceReport(boot,true,receipt(id,boot,"accepted")));
  elf="c".repeat(64);
  assertEquals(409,maintenance(maintenanceReport(boot,true,receipt(id,boot,"installed"))).statusCode());
  var unverified=JSON.valueToTree(maintenanceReport("2".repeat(32),true,receipt(id,boot,"installed")));
  ((com.fasterxml.jackson.databind.node.ObjectNode)unverified.path("system_info").path("ota")).put("state","pending_verify");
  assertEquals(409,maintenance(unverified).statusCode());
  assertEquals("unobserved",operationData(id).path("observation_status").asText());
  var installed=maintenance(maintenanceReport("2".repeat(32),true,receipt(id,boot,"installed")));
  assertEquals(200,installed.statusCode(),installed.body());
  assertEquals("target_running",operationData(id).path("observation_status").asText());
  assertEquals("accepted_unconfirmed",operationData(id).path("status").asText());
  assertEquals("already_running",managementPreview().path("reason").asText());
  assertEquals(200,maintenance(maintenanceReport("2".repeat(32),true,receipt(id,boot,"installed"))).statusCode());
  assertEquals(0,sent.get());
 }
 @Test void retiredBootCannotReplaceCurrentPresenceOrAcceptAnOldCommand() throws Exception {
  String first="1".repeat(32),second="2".repeat(32); admitManagement(first);
  assertEquals(200,maintenance(maintenanceReport(second,true,null)).statusCode());
  var stale=maintenance(maintenanceReport(first,true,null));
  assertEquals(409,stale.statusCode()); assertEquals("STALE_DEVICE_BOOT",JSON.readTree(stale.body()).path("code").asText());
  String id=managementOperation().path("operation_id").asText();
  var command=JSON.readTree(maintenance(maintenanceReport(second,true,null)).body()).path("data").path("command");
  assertEquals(second,command.path("boot_id").asText());
  assertEquals(409,maintenance(maintenanceReport(first,true,receipt(id,second,"accepted"))).statusCode());
  assertEquals("command_offered",operationData(id).path("status").asText());
 }
 @ParameterizedTest @ValueSource(booleans={false,true})
 void fixedCommandExpiryDistinguishesUnsentFromExposedWithoutReplay(boolean exposed) throws Exception {
  String boot="1".repeat(32); admitManagement(boot); String id=managementOperation().path("operation_id").asText();
  JsonNode first=null;
  if(exposed) {
   first=JSON.readTree(maintenance(maintenanceReport(boot,true,null)).body()).path("data").path("command");
   now.addAndGet(1000);
   var repeated=JSON.readTree(maintenance(maintenanceReport(boot,true,null)).body()).path("data").path("command");
   assertEquals(first.path("expires_at_ms"),repeated.path("expires_at_ms"));
   assertEquals(id,repeated.path("operation_id").asText());
  }
  now.addAndGet(600001);
  assertEquals(exposed ? "result_unknown" : "not_sent",operationData(id).path("status").asText());
  var response=JSON.readTree(maintenance(maintenanceReport(boot,true,null)).body()).path("data");
  assertTrue(response.path("command").isNull());
  if(exposed) {
   var ack=JSON.readTree(maintenance(maintenanceReport(boot,true,receipt(id,boot,"accepted"))).body()).path("data").path("operation_ack");
   assertEquals(0,ack.path("expires_in_ms").asLong());
   assertEquals("operation_pending",managementPreview().path("reason").asText());
  } else assertTrue(managementPreview().path("eligible").asBoolean());
 }
 @Test void delayedFailureReceiptCannotReleaseANewerDeviceReservation() throws Exception {
  String boot="1".repeat(32); admitManagement(boot); String old=managementOperation().path("operation_id").asText();
  maintenance(maintenanceReport(boot,true,null));
  assertEquals(200,maintenance(maintenanceReport(boot,true,receipt(old,boot,"failed"))).statusCode());
  String current=managementOperation().path("operation_id").asText();
  assertEquals(200,maintenance(maintenanceReport(boot,false,receipt(old,boot,"failed"))).statusCode());
  assertEquals("waiting_idle",operationData(current).path("status").asText());
  assertEquals("operation_pending",managementPreview().path("reason").asText());
  assertEquals(409,maintenance(maintenanceReport(boot,true,receipt(old,boot,"accepted"))).statusCode());
 }
 @ParameterizedTest @ValueSource(strings={"boot","credential","hardware","operator","firmware"})
 void managementRechecksAuthorityAndHardwareBeforeFirstCommand(String change) throws Exception {
  String boot="1".repeat(32); admitManagement(boot); String id=managementOperation().path("operation_id").asText();
  var report=JSON.valueToTree(maintenanceReport(boot,true,null));
  if(change.equals("boot")) ((com.fasterxml.jackson.databind.node.ObjectNode)report).put("boot_id","2".repeat(32));
  if(change.equals("hardware")) ((com.fasterxml.jackson.databind.node.ObjectNode)report.path("system_info").path("board")).put("type","unknown-p4");
  if(change.equals("operator")) db.update("UPDATE sys_user SET status=0 WHERE id=?",ADMIN);
  if(change.equals("firmware")) db.update("UPDATE dc_ota_firmware SET verification_status='rejected' WHERE firmware_id=?",FIRMWARE);
  if(change.equals("credential")) db.update("UPDATE dc_device SET credential_hash=? WHERE device_id='ota97-device'","b".repeat(64));
  var response=maintenance(report);
  if(change.equals("credential")) assertEquals(401,response.statusCode());
  else { assertEquals(200,response.statusCode(),response.body()); assertTrue(JSON.readTree(response.body()).path("data").path("command").isNull()); }
  if(change.equals("operator")) db.update("UPDATE sys_user SET status=1 WHERE id=?",ADMIN);
  if(!change.equals("credential")) assertEquals("not_sent",operationData(id).path("status").asText());
  assertEquals(0,sent.get());
 }
 @ParameterizedTest @ValueSource(strings={"unbound","device_disabled"})
 void managementUpgradeDoesNotRequireOrChangeCustomerPermission(String reason) throws Exception {
  String boot="1".repeat(32); db.update("UPDATE dc_device SET reason=?,local_enabled=false WHERE device_id='ota97-device'",reason);
  admitManagement(boot); String id=managementOperation().path("operation_id").asText();
  assertEquals(id,JSON.readTree(maintenance(maintenanceReport(boot,true,null)).body()).path("data").path("command").path("operation_id").asText());
  assertEquals(200,maintenance(maintenanceReport(boot,true,receipt(id,boot,"accepted"))).statusCode());
  var state=http.send(HttpRequest.newBuilder(URI.create("http://localhost:"+port+"/xiaozhi/device-control/v1/state"))
   .header("Authorization","Bearer "+DEVICE_CREDENTIAL).header("Device-Id","ota97-device").header("X-Forwarded-Proto","https").GET().build(),HttpResponse.BodyHandlers.ofString());
  var data=JSON.readTree(state.body()).path("data");
  assertEquals(reason,data.path("reason").asText()); assertEquals("blocked",data.path("use_permission").asText());
  assertEquals(1,data.path("permission_version").asLong());
 }
 @Test void legacyPollCannotRefreshExpiredManagementCapabilityAndMalformedReportsCannotRefreshOnline() throws Exception {
  connected=true; offlineDevices.add("ota97-device");
  String boot="1".repeat(32); admitManagement(boot); now.addAndGet(90001);
  var malformed=JSON.valueToTree(maintenanceReport(boot,true,null));
  ((com.fasterxml.jackson.databind.node.ObjectNode)malformed.path("system_info").path("application")).put("elf_sha256","bad");
  assertEquals(400,maintenance(malformed).statusCode());
  assertFalse(batchItem(JSON.readTree(get("devices?limit=100",TOKEN).body()).path("data"),"ota97-device").path("online").asBoolean());
  var state=http.send(HttpRequest.newBuilder(URI.create("http://localhost:"+port+"/xiaozhi/device-control/v1/state"))
   .header("Authorization","Bearer "+DEVICE_CREDENTIAL).header("Device-Id","ota97-device").header("X-Forwarded-Proto","https").GET().build(),HttpResponse.BodyHandlers.ofString());
  assertEquals(200,state.statusCode());
  var device=batchItem(JSON.readTree(get("devices?limit=100",TOKEN).body()).path("data"),"ota97-device");
  assertTrue(device.path("online").asBoolean()); assertEquals("online_requires_wake",device.path("status").asText());
  assertFalse(managementPreview().path("eligible").asBoolean());
 }
 @Test void allOnlineBatchUsesManagementEvenWhenTheFirstVoiceProbeFails() throws Exception {
  String boot="1".repeat(32); admitManagement(boot);
  db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-aaa-offline')");
  var preview=JSON.readTree(post("batch-preview",Map.of("mode","all_online","device_ids",List.of(),"firmware_id",FIRMWARE)).body()).path("data");
  assertEquals(1,preview.path("eligible_count").asInt(),preview.toString());
  assertFalse(batchItem(preview,"ota97-aaa-offline").path("eligible").asBoolean());
  assertEquals("management",batchItem(preview,"ota97-device").path("ota_transport").asText());
  var batch=JSON.readTree(post("batches",Map.of("preview_id",preview.path("preview_id").asText(),"request_key","5".repeat(32))).body()).path("data");
  assertEquals("waiting_idle",batchItem(batch,"ota97-device").path("status").asText());
  assertEquals("management",batchItem(batch,"ota97-device").path("ota_transport").asText());
  connected=true; now.addAndGet(90001); // Legacy voice becomes usable, but shares the same durable device claim.
  assertEquals("operation_pending",managementPreview().path("reason").asText());
  assertEquals(0,sent.get());
 }
 @Test void managementConfirmationRetainsThePreviewArtifactAndRejectsNumericBootIdentifiers() throws Exception {
  String boot="1".repeat(32); admitManagement(boot);
  var malformed=JSON.valueToTree(maintenanceReport(boot,true,null));
  ((com.fasterxml.jackson.databind.node.ObjectNode)malformed).put("boot_id",new java.math.BigInteger(boot));
  assertEquals(400,maintenance(malformed).statusCode());
  var preview=managementPreview(); assertTrue(preview.path("eligible").asBoolean());
  db.update("UPDATE dc_ota_firmware SET sha256=? WHERE firmware_id=?","e".repeat(64),FIRMWARE);
  var operation=JSON.readTree(post("operations",Map.of("preview_id",preview.path("preview_id").asText(),"request_key","6".repeat(32))).body()).path("data");
  assertEquals("not_sent",operation.path("status").asText());
  assertEquals("artifact_changed",operation.path("reason").asText());
 }
 @Test void rebootBeforeInstallationReportsInterruptionWithoutIssuingTheCommandAgain() throws Exception {
  String boot="1".repeat(32); admitManagement(boot); String id=managementOperation().path("operation_id").asText();
  maintenance(maintenanceReport(boot,true,null));
  maintenance(maintenanceReport(boot,true,receipt(id,boot,"accepted")));
  var response=maintenance(maintenanceReport("2".repeat(32),true,receipt(id,boot,"interrupted")));
  assertEquals(200,response.statusCode(),response.body());
  var data=JSON.readTree(response.body()).path("data");
  assertEquals("interrupted",data.path("operation_ack").path("status").asText());
  assertTrue(data.path("command").isNull());
  assertEquals("result_requires_verification",operationData(id).path("status").asText());
  assertEquals("operation_pending",managementPreview().path("reason").asText());
  assertEquals(200,maintenance(maintenanceReport("2".repeat(32),true,receipt(id,boot,"interrupted"))).statusCode());
 }
 @Test void productionFirmwareRequestProjectionWorksWithTheRealManagementHttpInterface() throws Exception {
  db.update("UPDATE dc_device SET credential_hash=? WHERE device_id='ota97-device'",ProductOtaCatalog.digest(DEVICE_CREDENTIAL.getBytes(java.nio.charset.StandardCharsets.UTF_8)));
  JsonNode report;
  try(var input=getClass().getResourceAsStream("/device-maintenance/firmware-request.json")) { report=JSON.readTree(input); }
  var contact=maintenance(report); assertEquals(200,contact.statusCode(),contact.body());
  var preview=managementPreview();
  assertEquals("f242b5b68cc3c565335c7eb0b048156209531ebd6fb5e9c54bdf809b9cc58d6e",preview.path("current_elf_sha256").asText());
  assertTrue(preview.path("eligible").asBoolean(),preview.toString());
  String id=managementOperation().path("operation_id").asText();
  var response=maintenance(report); assertEquals(200,response.statusCode(),response.body());
  assertEquals(id,JSON.readTree(response.body()).path("data").path("command").path("operation_id").asText());
  java.nio.file.Files.writeString(java.nio.file.Path.of("target/maintenance-response.json"),response.body());
 }
 @Test void legacyVoiceBootstrapCompletesFromFreshManagementReportWithoutWakingVoiceAgain() throws Exception {
  connected=true;
  db.update("UPDATE dc_device SET credential_hash=? WHERE device_id='ota97-device'",ProductOtaCatalog.digest(DEVICE_CREDENTIAL.getBytes(java.nio.charset.StandardCharsets.UTF_8)));
  String id=managementOperation().path("operation_id").asText();
  assertEquals("voice",operationData(id).path("ota_transport").asText());
  connected=false; now.incrementAndGet(); elf="c".repeat(64);
  assertEquals(200,maintenance(maintenanceReport("2".repeat(32),true,null)).statusCode());
  assertEquals("target_running",operationData(id).path("observation_status").asText());
  assertEquals("accepted_unconfirmed",operationData(id).path("status").asText());
  assertEquals("already_running",managementPreview().path("reason").asText());
  assertEquals(1,sent.get());
 }
 @Test void managementRunningEvidenceCanCompleteWithoutReceiptButNeverAcrossCredentialRotation() throws Exception {
  String boot="1".repeat(32); admitManagement(boot); String id=managementOperation().path("operation_id").asText();
  maintenance(maintenanceReport(boot,true,null));
  maintenance(maintenanceReport(boot,true,receipt(id,boot,"accepted")));
  elf="c".repeat(64); now.incrementAndGet();
  assertEquals(200,maintenance(maintenanceReport("2".repeat(32),true,null)).statusCode());
  assertEquals("target_running",operationData(id).path("observation_status").asText());
  var delayed=maintenance(maintenanceReport("2".repeat(32),true,receipt(id,boot,"installed")));
  assertEquals(200,delayed.statusCode(),delayed.body());
  assertEquals("installed",JSON.readTree(delayed.body()).path("data").path("operation_ack").path("status").asText());
  assertEquals(409,maintenance(maintenanceReport("2".repeat(32),true,receipt(id,boot,"failed"))).statusCode());
  cleanup(); setup(); connected=true;
  String otherCredential="z".repeat(64);
  db.update("UPDATE dc_device SET credential_hash=? WHERE device_id='ota97-device'",otherCredential);
  String old=managementOperation().path("operation_id").asText();
  connected=false; elf="c".repeat(64); now.incrementAndGet();
  admitManagement("3".repeat(32));
  assertEquals("unobserved",operationData(old).path("observation_status").asText());
  assertEquals("operation_pending",managementPreview().path("reason").asText());
 }
 @ParameterizedTest @ValueSource(booleans={false,true})
 void durableFailedReceiptCanBeAcknowledgedAfterRebootWithoutHidingRunningEvidence(boolean running) throws Exception {
  String boot="1".repeat(32); admitManagement(boot); String id=managementOperation().path("operation_id").asText();
  maintenance(maintenanceReport(boot,true,null));
  maintenance(maintenanceReport(boot,true,receipt(id,boot,"accepted")));
  if(running) elf="c".repeat(64);
  var failed=maintenance(maintenanceReport("2".repeat(32),true,receipt(id,boot,"failed")));
  assertEquals(200,failed.statusCode(),failed.body());
  assertEquals("failed",JSON.readTree(failed.body()).path("data").path("operation_ack").path("status").asText());
  assertEquals(running ? "target_running" : "unobserved",operationData(id).path("observation_status").asText());
  if(!running) {
   assertEquals("failed",operationData(id).path("status").asText());
   assertTrue(managementPreview().path("eligible").asBoolean());
   String next=managementOperation().path("operation_id").asText();
   assertEquals(200,maintenance(maintenanceReport("2".repeat(32),false,receipt(id,boot,"failed"))).statusCode());
   assertEquals("waiting_idle",operationData(next).path("status").asText());
  } else assertEquals("already_running",managementPreview().path("reason").asText());
 }
 @ParameterizedTest @ValueSource(strings={"current","offline","bridge_unavailable"})
 void inventoryShowsActualVoiceStateWithoutChangingManagementEligibility(String voiceState) throws Exception {
  String boot="1".repeat(32); admitManagement(boot);
  connected=!voiceState.equals("bridge_unavailable");
  if(voiceState.equals("offline")) offlineDevices.add("ota97-device");
  var device=batchItem(JSON.readTree(get("devices?limit=100",TOKEN).body()).path("data"),"ota97-device");
  assertEquals(voiceState,device.path("voice_status").asText(),device.toString());
  assertTrue(device.path("online").asBoolean()); assertEquals("current",device.path("status").asText());
  assertEquals("management",device.path("ota_transport").asText());
  assertTrue(managementPreview().path("eligible").asBoolean());
 }
 @Test void hardwareReportMustRemainIdenticalEvenWhenBothVersionsWouldBeCompatible() throws Exception {
  String boot="1".repeat(32); admitManagement(boot); String id=managementOperation().path("operation_id").asText();
  var changed=JSON.valueToTree(maintenanceReport(boot,true,null));
  ((com.fasterxml.jackson.databind.node.ObjectNode)changed.path("system_info")).put("flash_size",33554432);
  var response=maintenance(changed); assertEquals(200,response.statusCode(),response.body());
  assertTrue(JSON.readTree(response.body()).path("data").path("command").isNull());
  assertEquals("not_sent",operationData(id).path("status").asText());
  assertEquals("hardware_changed",operationData(id).path("reason").asText());
 }
 @Test void failedArmingReservationExpiresWithoutNeedingAnApiRestart() throws Exception {
  String boot="1".repeat(32); admitManagement(boot); var preview=managementPreview();
  db.execute("CREATE TRIGGER maintenance_expiry_failure BEFORE UPDATE ON dc_ota_operation FOR EACH ROW "
    +"BEGIN IF OLD.device_id='ota97-device' AND OLD.status='dispatch_reserved' AND NEW.status='waiting_idle' "
    +"THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture arm interruption'; END IF; END");
  String id;
  try {
   var response=post("operations",Map.of("preview_id",preview.path("preview_id").asText(),"request_key","3".repeat(32)));
   assertEquals(503,response.statusCode(),response.body());
   var operation=JSON.readTree(get("operations",TOKEN).body()).path("data").get(0);
   id=operation.path("operation_id").asText();
   assertEquals("dispatch_reserved",operation.path("status").asText());
  } finally { db.execute("DROP TRIGGER maintenance_expiry_failure"); }
  now.addAndGet(600001);
  assertEquals("not_sent",operationData(id).path("status").asText());
  assertEquals("command_expired",operationData(id).path("reason").asText());
  var contact=maintenance(maintenanceReport(boot,true,null)); assertEquals(200,contact.statusCode(),contact.body());
  assertTrue(JSON.readTree(contact.body()).path("data").path("command").isNull());
  assertTrue(managementPreview().path("eligible").asBoolean());
 }
}
