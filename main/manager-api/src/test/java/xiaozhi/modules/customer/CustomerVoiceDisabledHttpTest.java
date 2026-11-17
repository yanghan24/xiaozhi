package xiaozhi.modules.customer;

import static org.junit.jupiter.api.Assertions.*;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.jdbc.core.JdbcTemplate;

@SpringBootTest(webEnvironment=SpringBootTest.WebEnvironment.RANDOM_PORT,properties={
    "server.servlet.context-path=/xiaozhi","server.forward-headers-strategy=native",
    "device-control.enabled=true","device-control.voice-enabled=false",
    "device-control.public-origin=https://bridge.hncig.cn:18443"})
class CustomerVoiceDisabledHttpTest {
    @Autowired TestRestTemplate http;
    @Autowired JdbcTemplate db;
    @Test void disablingVoiceInvalidatesPersistedReadyPermission() {
        var client=new BindingTestClient(http,db,null);var device=client.device();
        // Durable fixture from the previous enabled deployment.
        db.update("UPDATE dc_device SET owner_issuer='old',owner_subject='owner',local_enabled=true,reason='ready' WHERE device_id=?",device.id());
        var state=client.state(device);
        assertEquals("blocked",state.get("use_permission"));
        assertEquals("device_disabled",state.get("reason"));
        assertEquals(2,((Number)state.get("permission_version")).intValue());
        assertNull(state.get("voice"));assertNull(state.get("lease"));
        assertEquals(state.get("permission_version"),client.state(device).get("permission_version"));
    }
}
