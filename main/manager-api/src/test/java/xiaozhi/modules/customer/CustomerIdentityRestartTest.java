package xiaozhi.modules.customer;

import static org.junit.jupiter.api.Assertions.*;
import java.util.*;
import java.time.Duration;
import org.junit.jupiter.api.Test;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.boot.web.servlet.context.ServletWebServerApplicationContext;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.http.*;
import org.springframework.jdbc.core.JdbcTemplate;
import xiaozhi.AdminApplication;

/** Starts/stops real applications over the same MySQL/Redis, including concurrent instances. */
class CustomerIdentityRestartTest {
    static final ControlledMarket market = new ControlledMarket();
    ServletWebServerApplicationContext start() {
        return (ServletWebServerApplicationContext) new SpringApplicationBuilder(AdminApplication.class, CustomerTestTime.class)
                .run("--server.port=0", "--server.servlet.context-path=/xiaozhi", "--spring.main.banner-mode=off", "--renren.xss.enabled=true",
                        "--customer.oidc.config-file=" + market.config, "--customer.oidc.refresh-delay-ms=200");
    }
    TestRestTemplate http(ServletWebServerApplicationContext app) {
        return new TestRestTemplate(new org.springframework.boot.web.client.RestTemplateBuilder()
                .rootUri("http://127.0.0.1:" + app.getWebServer().getPort() + "/xiaozhi"));
    }
    @Test
    void identityRefreshChainAndLogoutDedupSurviveRestartAndConcurrentInstances() throws Exception {
        CustomerTestTime.offset.set(0);
        HttpHeaders headers = new HttpHeaders();
        String id, original, token;
        try (var first = start()) {
            var db = first.getBean(JdbcTemplate.class);
            String device = UUID.randomUUID().toString().replace("-", ""), handle = UUID.randomUUID().toString();
            db.update("INSERT INTO ai_device(id,alias) VALUES (?,?)", device, "Restart speaker");
            db.update("INSERT INTO customer_device_request(handle,device_id,purpose,permission_version,expires_at_ms) VALUES (?,?,'bind',1,?)", handle, device, System.currentTimeMillis()+600_000);
            var http = http(first);
            var page = http.getForEntity("/bind/"+handle, String.class);
            headers.set("Cookie", page.getHeaders().getFirst("Set-Cookie").split(";",2)[0]);
            headers.set("X-CSRF-Token", page.getBody().split("name=\"csrf\" value=\"")[1].split("\"")[0]);
            headers.setOrigin("https://bridge.hncig.cn:18443");
            var flow = http.postForEntity("/customer/binding-flows",new HttpEntity<>(Map.of("request_handle",handle),headers),Map.class);
            var data = (Map<?,?>) flow.getBody().get("data"); id=data.get("flow_id").toString();
            String code = market.approve(data.get("authorization_url").toString());
            var auth = Map.copyOf(market.codes.get(code));
            http.exchange("/customer/oidc/callback?code="+code+"&state="+auth.get("state"),HttpMethod.GET,new HttpEntity<>(headers),String.class);
            original=market.refreshes.entrySet().stream().filter(e -> auth.get("sub").equals(e.getValue().get("sub"))).findFirst().orElseThrow().getKey();
            token=market.logout(Map.of("sid",auth.get("sid")));
            assertEquals(204, logout(http,token));
            assertEquals("SESSION_ENDED",status(http,headers,id).get("status"));
        }
        try (var second=start(); var concurrent=start()) {
            var http=http(second);
            assertEquals("SESSION_ENDED",status(http,headers,id).get("status"));
            assertEquals(204,logout(http(concurrent),token));
            CustomerTestTime.offset.set(271_000);
            long deadline=System.nanoTime()+Duration.ofSeconds(8).toNanos();
            while (!market.refreshAttempts.containsKey(original) && System.nanoTime()<deadline) Thread.sleep(50);
            Thread.sleep(500);
            assertEquals(1,market.refreshAttempts.get(original).get());
            assertEquals("ACTIVE",status(http,headers,id).get("authorization_status"));
        }
        try (var restarted=start()) {
            assertEquals("ACTIVE",status(http(restarted),headers,id).get("authorization_status"));
            assertEquals("SESSION_ENDED",status(http(restarted),headers,id).get("status"));
            assertEquals(1,market.refreshAttempts.get(original).get());
            CustomerTestTime.offset.set(1_801_000);
            assertEquals(401,http(restarted).exchange("/customer/binding-flows/"+id,HttpMethod.GET,new HttpEntity<>(headers),String.class).getStatusCode().value());
        } finally { CustomerTestTime.offset.set(0); }
    }
    Map<?,?> status(TestRestTemplate http,HttpHeaders headers,String id) {
        var response=http.exchange("/customer/binding-flows/"+id,HttpMethod.GET,new HttpEntity<>(headers),Map.class);
        assertEquals(200,response.getStatusCode().value()); return (Map<?,?>) response.getBody().get("data");
    }
    int logout(TestRestTemplate http,String token) {
        var headers=new HttpHeaders();headers.setContentType(MediaType.APPLICATION_FORM_URLENCODED);
        var body=new org.springframework.util.LinkedMultiValueMap<String,String>();body.add("logout_token",token);
        return http.postForEntity("/customer/oidc/backchannel-logout",new HttpEntity<>(body,headers),String.class).getStatusCode().value();
    }
}
