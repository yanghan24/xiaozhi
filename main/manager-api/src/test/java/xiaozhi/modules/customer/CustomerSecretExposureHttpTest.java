package xiaozhi.modules.customer;

import static org.junit.jupiter.api.Assertions.*;

import java.util.ArrayList;
import java.util.Map;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.system.CapturedOutput;
import org.springframework.boot.test.system.OutputCaptureExtension;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.http.HttpEntity;
import org.springframework.http.HttpMethod;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

/** A19: real HTTP surfaces and application output, with only the market substituted. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT, properties = {
        "server.servlet.context-path=/xiaozhi", "server.forward-headers-strategy=native",
        "renren.xss.enabled=true", "device-control.enabled=true",
        "device-control.public-origin=https://bridge.hncig.cn:18443"})
@ExtendWith(OutputCaptureExtension.class)
class CustomerSecretExposureHttpTest {
    static final ControlledMarket market = new ControlledMarket();
    @DynamicPropertySource static void properties(DynamicPropertyRegistry registry) {
        registry.add("customer.oidc.config-file", () -> market.config.toString());
    }
    @Autowired TestRestTemplate http;
    @Autowired JdbcTemplate db;

    @Test void bindingPagesDeviceResponsesAndLogsDoNotExposeSecrets(CapturedOutput output) {
        var client = new BindingTestClient(http, db, market);
        var device = client.device();
        var surfaces = new ArrayList<String>();
        surfaces.add(http.getForEntity("/bind/" + device.handle(), String.class).toString());
        surfaces.add(client.state(device).toString()); // Includes the QR verification URI.
        var flow = client.scan(device);
        surfaces.add(flow.url());
        String code = market.approve(flow.url());
        String state = market.codes.get(code).get("state");
        var callback = http.exchange("/customer/oidc/callback?code=" + code + "&state=" + state,
                HttpMethod.GET, new HttpEntity<>(flow.headers()), String.class);
        assertEquals(200, callback.getStatusCode().value());
        surfaces.add(callback.toString());
        assertEquals("IDENTITY_VERIFIED", client.status(flow).get("status"));
        assertEquals("ACTIVE", client.status(flow).get("authorization_status"));
        surfaces.add(client.status(flow).toString());
        var confirmed = client.confirm(flow, UUID.randomUUID().toString());
        assertEquals(200, confirmed.getStatusCode().value());
        surfaces.add(confirmed.toString());
        surfaces.add(client.page(flow));
        surfaces.add(client.state(device).toString());
        var malformed = http.postForEntity("/device-control/v1/requests",
                new HttpEntity<>(Map.of("unexpected", device.credential()),
                        BindingTestClient.deviceHeaders(device)), String.class);
        assertEquals(400, malformed.getStatusCode().value());
        surfaces.add(malformed.toString());
        var forbidden = new ArrayList<String>();
        forbidden.add(device.credential());
        forbidden.add(code);
        forbidden.add("test-only-secret");
        forbidden.add("test-business-key");
        assertFalse(market.accesses.isEmpty(), "The HTTP flow must actually issue access tokens");
        assertFalse(market.refreshes.isEmpty(), "The HTTP flow must actually issue refresh tokens");
        forbidden.addAll(market.accesses.keySet());
        forbidden.addAll(market.refreshes.keySet());
        surfaces.add(output.getAll());
        for (int surface = 0; surface < surfaces.size(); surface++) {
            for (int secret = 0; secret < forbidden.size(); secret++) {
                // Never include a secret or an HTTP body in a failing assertion message.
                assertFalse(surfaces.get(surface).contains(forbidden.get(secret)),
                        "Secret category " + secret + " appeared on surface " + surface);
            }
        }
    }
}
