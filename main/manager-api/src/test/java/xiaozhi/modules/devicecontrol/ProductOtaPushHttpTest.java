package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.*;
import com.fasterxml.jackson.databind.*;
import java.net.*;
import java.net.http.*;
import java.util.*;
import org.junit.jupiter.api.*;
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
 properties={"server.servlet.context-path=/xiaozhi","server.forward-headers-strategy=native"})
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
 static final String FIRMWARE="9".repeat(32);
 static final AtomicInteger sent=new AtomicInteger();
 @DynamicPropertySource static void properties(DynamicPropertyRegistry registry) throws Exception {
  voice=HttpServer.create(new InetSocketAddress("127.0.0.1",0),0);
  voice.createContext("/internal/product-ota/v1/control",exchange->{
   var request=JSON.readTree(exchange.getRequestBody());
   Object result=Map.of("status","bridge_unavailable");
   if(connected) {
    String action=request.path("action").asText();
    if(action.equals("inspect") && inspectUnavailable) { exchange.sendResponseHeaders(503,-1); exchange.close(); return; }
    if(action.equals("status")) result=Map.of("status","current","session_id",session,"permission_version",1);
    if(action.equals("inspect")) result=Map.of("status","ready","system_info",Map.of(
     "board",Map.of("type","waveshare-esp32-p4-wifi6-touch-lcd-4.3"),"flash_size",33554432,
     "chip_info",Map.of("model",18,"revision",100),"application",Map.of("version","2.2.6","elf_sha256",elf),
     "ota",Map.of("label","ota_0"),"partition_table",List.of(
      partition("nvs",1,2,0x9000,0x4000),partition("otadata",1,0,0xd000,0x2000),
      partition("phy_init",1,1,0xf000,0x1000),partition("ota_0",0,16,0x20000,0x3f0000),
      partition("ota_1",0,17,0x410000,0x3f0000),partition("assets",1,130,0x800000,0x800000))));
    if(action.equals("inspect") && !hardwareCase.equals("normal")) {
     var changed=(com.fasterxml.jackson.databind.node.ObjectNode)JSON.valueToTree(result);
     var info=(com.fasterxml.jackson.databind.node.ObjectNode)changed.path("system_info");
     if(hardwareCase.equals("missing")) info.remove("chip_info");
     if(hardwareCase.equals("small-slot")) ((com.fasterxml.jackson.databind.node.ObjectNode)info.path("partition_table").get(4)).put("size",2048);
     result=changed;
    }
    if(action.equals("dispatch")) {
     sent.incrementAndGet(); downloadUrl=request.path("url").asText();
     if(loseReply) { exchange.sendResponseHeaders(503,-1); exchange.close(); return; }
     result=Map.of("status","accepted_unconfirmed");
    }
    if(action.equals("reconcile")) result=elf.equals("c".repeat(64)) && !session.equals("session-97")
      ? Map.of("status","result_unknown","observation_status","target_running","observed_at_ms",System.currentTimeMillis()) : Map.of("status","result_unknown");
   }
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
  connected=false; sent.set(0); elf="a".repeat(64); downloadUrl=null; loseReply=false; inspectUnavailable=false; session="session-97"; hardwareCase="normal";
  db.update("INSERT INTO sys_user(id,username,super_admin,status) VALUES (?, 'ota97-admin',1,1)",ADMIN);
  db.update("INSERT INTO sys_user_token(id,user_id,token,expire_date) VALUES (?,?,?,DATE_ADD(CURRENT_TIMESTAMP,INTERVAL 1 DAY))",ADMIN,ADMIN,TOKEN);
  db.update("INSERT INTO dc_device(device_id) VALUES ('ota97-device')");
  db.update("INSERT INTO dc_ota_firmware(firmware_id,github_release_id,github_asset_id,release_tag,prerelease,source_commit,source_url,manifest_sha256,sha256,elf_sha256,app_version,board_type,flash_size,min_revision,max_revision,partition_layout,partition_table_sha256,app_partition_bytes,image_size,image,verification_status,operator_id,created_at_ms) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
   FIRMWARE,9701,97010,"test",false,"a".repeat(40),"https://example.test","b".repeat(64),ProductOtaCatalog.digest(new byte[]{1,2,3}),"c".repeat(64),"2.2.6","waveshare-esp32-p4-wifi6-touch-lcd-4.3",16777216,100,399,"partitions/v2/16m.csv","d".repeat(64),4128768,3,new byte[]{1,2,3},"verified",ADMIN,1);
 }
 @AfterEach void cleanup() {
  db.update("DELETE FROM dc_ota_operation WHERE operator_id=?",ADMIN);
  db.update("DELETE FROM dc_ota_preview WHERE operator_id=?",ADMIN);
  db.update("DELETE FROM dc_ota_firmware WHERE operator_id=?",ADMIN);
  db.update("DELETE FROM dc_device WHERE device_id LIKE 'ota97-%'");
  db.update("DELETE FROM sys_user_token WHERE user_id=?",ADMIN);
  db.update("DELETE FROM sys_user WHERE id=?",ADMIN);
 }
 HttpResponse<String> post(String path,Object body) throws Exception {
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
}
