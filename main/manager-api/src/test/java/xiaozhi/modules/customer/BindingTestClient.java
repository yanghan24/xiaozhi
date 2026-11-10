package xiaozhi.modules.customer;

import static org.junit.jupiter.api.Assertions.*;
import java.net.URI;
import java.util.Map;
import java.util.UUID;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.http.*;
import org.springframework.jdbc.core.JdbcTemplate;

/** Registration is a fixture; all customer/device behavior is observed through HTTP. */
final class BindingTestClient {
    record Device(String id, String credential, String handle) {}
    record Flow(HttpHeaders headers, String id, String url) {}
    final TestRestTemplate http;
    final JdbcTemplate db;
    final ControlledMarket market;
    BindingTestClient(TestRestTemplate http, JdbcTemplate db, ControlledMarket market) {
        this.http=http; this.db=db; this.market=market;
    }
    Device device() {
        String id="bind-"+UUID.randomUUID(), credential=CustomerSecrets.random();
        db.update("INSERT INTO dc_device(device_id,credential_hash) VALUES (?,?)",id,CustomerSecrets.hash(credential));
        var headers=deviceHeaders(new Device(id,credential,null));
        headers.set("Idempotency-Key",UUID.randomUUID().toString());
        var response=http.postForEntity("/device-control/v1/requests",new HttpEntity<>(Map.of("intent","auto","expected_permission_version",1),headers),Map.class);
        assertEquals(200,response.getStatusCode().value());
        var request=(Map<?,?>)data(response).get("binding_request");
        String path=URI.create(request.get("verification_uri").toString()).getPath();
        return new Device(id,credential,path.substring(path.lastIndexOf('/')+1));
    }
    Device request(Device device,String intent) {
        var headers=deviceHeaders(device);headers.set("Idempotency-Key",UUID.randomUUID().toString());
        var response=http.postForEntity("/device-control/v1/requests",new HttpEntity<>(Map.of("intent",intent,"expected_permission_version",state(device).get("permission_version")),headers),Map.class);
        assertEquals(200,response.getStatusCode().value());
        String path=URI.create(((Map<?,?>)data(response).get("binding_request")).get("verification_uri").toString()).getPath();
        return new Device(device.id(),device.credential(),path.substring(path.lastIndexOf('/')+1));
    }
    ResponseEntity<Map> post(Flow flow,String path,String key) {
        var headers=new HttpHeaders();headers.putAll(flow.headers());headers.set("Idempotency-Key",key);
        return http.postForEntity("/customer/v1/"+path,new HttpEntity<>(Map.of(),headers),Map.class);
    }
    ResponseEntity<Map> get(Flow flow,String path) {
        return http.exchange("/customer/v1/"+path,HttpMethod.GET,new HttpEntity<>(flow.headers()),Map.class);
    }
    Flow scan(Device device) {
        var page=http.getForEntity("/bind/"+device.handle(),String.class);
        assertEquals(200,page.getStatusCode().value());
        var headers=new HttpHeaders();
        headers.set("Cookie",page.getHeaders().getFirst("Set-Cookie").split(";",2)[0]);
        headers.set("X-CSRF-Token",page.getBody().split("name=\"csrf\" value=\"")[1].split("\"")[0]);
        headers.setOrigin("https://bridge.hncig.cn:18443");
        var created=http.postForEntity("/customer/v1/binding-flows",new HttpEntity<>(Map.of("request_handle",device.handle()),headers),Map.class);
        assertEquals(201,created.getStatusCode().value());
        return new Flow(headers,data(created).get("flow_id").toString(),data(created).get("authorization_url").toString());
    }
    Map<String,String> login(Flow flow, String fault, String subject) {
        String code=market.approve(flow.url());
        var auth=market.codes.get(code);
        if (fault!=null) auth.put("fault",fault);
        if (subject!=null) auth.put("sub",subject);
        var saved=Map.copyOf(auth);
        http.exchange("/customer/oidc/callback?code="+code+"&state="+auth.get("state"),HttpMethod.GET,new HttpEntity<>(flow.headers()),String.class);
        return saved;
    }
    ResponseEntity<Map> confirm(Flow flow, String key) {
        var headers=new HttpHeaders(); headers.putAll(flow.headers()); headers.set("Idempotency-Key",key);
        return http.postForEntity("/customer/v1/binding-flows/"+flow.id()+"/confirm",new HttpEntity<>(Map.of(),headers),Map.class);
    }
    Map<?,?> state(Device device) {
        return data(http.exchange("/device-control/v1/state",HttpMethod.GET,new HttpEntity<>(deviceHeaders(device)),Map.class));
    }
    Map<?,?> status(Flow flow) {
        return data(http.exchange("/customer/v1/binding-flows/"+flow.id(),HttpMethod.GET,new HttpEntity<>(flow.headers()),Map.class));
    }
    String page(Flow flow) {
        return http.exchange("/customer/results/"+flow.id(),HttpMethod.GET,new HttpEntity<>(flow.headers()),String.class).getBody();
    }
    static Map<?,?> data(ResponseEntity<Map> response) { return (Map<?,?>)response.getBody().get("data"); }
    static HttpHeaders deviceHeaders(Device device) {
        var headers=new HttpHeaders(); headers.setBearerAuth(device.credential());
        headers.set("Device-Id",device.id()); headers.set("X-Forwarded-Proto","https"); return headers;
    }
}
