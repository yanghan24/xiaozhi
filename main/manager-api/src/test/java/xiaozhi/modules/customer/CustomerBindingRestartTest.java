package xiaozhi.modules.customer;

import static org.junit.jupiter.api.Assertions.*;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.*;
import org.junit.jupiter.api.Test;
import org.springframework.boot.builder.SpringApplicationBuilder;
import org.springframework.boot.web.servlet.context.ServletWebServerApplicationContext;
import org.springframework.boot.context.logging.LoggingApplicationListener;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.boot.web.client.RestTemplateBuilder;
import org.springframework.jdbc.core.JdbcTemplate;
import xiaozhi.AdminApplication;

class CustomerBindingRestartTest {
    static final ControlledMarket market=new ControlledMarket();
    ServletWebServerApplicationContext start() {
        var builder=new SpringApplicationBuilder(AdminApplication.class,CustomerTestTime.class);
        builder.application().setListeners(builder.application().getListeners().stream()
                .filter(listener -> !(listener instanceof LoggingApplicationListener)).toList());
        return (ServletWebServerApplicationContext)builder.run("--server.port=0","--server.servlet.context-path=/xiaozhi",
                "--server.forward-headers-strategy=native","--device-control.enabled=true","--renren.xss.enabled=true",
                "--device-control.public-origin=https://bridge.hncig.cn:18443","--customer.oidc.config-file="+market.config);
    }
    BindingTestClient client(ServletWebServerApplicationContext app) {
        return new BindingTestClient(new TestRestTemplate(new RestTemplateBuilder().rootUri(
                "http://127.0.0.1:"+app.getWebServer().getPort()+"/xiaozhi")),app.getBean(JdbcTemplate.class),market);
    }
    @Test void crossInstanceWinnerAndRetryReceiptSurviveApplicationRestart() throws Exception {
        CustomerTestTime.offset.set(0);
        BindingTestClient.Device device;
        BindingTestClient.Flow winner;
        Map<?,?> original;
        try (var first=start(); var second=start()) {
            var a=client(first);var b=client(second);device=a.device();
            var left=a.scan(device);var right=b.scan(device);a.login(left,null,null);b.login(right,null,null);
            var gate=new CountDownLatch(1);
            var one=CompletableFuture.supplyAsync(()->{ await(gate);return a.confirm(left,"stable"); });
            var two=CompletableFuture.supplyAsync(()->{ await(gate);return b.confirm(right,"stable"); });
            gate.countDown();
            var x=one.get(10,TimeUnit.SECONDS);var y=two.get(10,TimeUnit.SECONDS);
            assertEquals(Set.of(200,409),Set.of(x.getStatusCode().value(),y.getStatusCode().value()));
            winner=x.getStatusCode().value()==200 ? left : right;
            original=(x.getStatusCode().value()==200 ? x : y).getBody();
            assertEquals("bound",b.state(device).get("binding_state"));
        }
        try (var restarted=start()) {
            var c=client(restarted);
            CustomerTestTime.offset.set(601_000); // Success replay survives the original QR deadline.
            assertEquals(original,c.confirm(winner,"stable").getBody());
            String page=c.page(winner);
            assertTrue(page.contains("绑定成功"),page.replaceAll("value=\"[^\"]*\"","value=\"redacted\""));
            assertEquals("bound",c.state(device).get("binding_state"));
            assertEquals(2,((Number)c.state(device).get("permission_version")).intValue());
            CustomerTestTime.offset.set(2_402_000);
            assertEquals(401,c.confirm(winner,"stable").getStatusCode().value());
            assertEquals("bound",c.state(device).get("binding_state"));
        } finally { CustomerTestTime.offset.set(0); }
    }
    private static void await(CountDownLatch gate) {
        try { gate.await(); } catch (InterruptedException e) { Thread.currentThread().interrupt(); throw new RuntimeException(e); }
    }
}
