package xiaozhi.modules.speaker;

import java.util.Map;
import org.junit.jupiter.api.Test;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.SimpleTransactionStatus;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

class SpeakerMemoOwnershipTest {
    private final JdbcTemplate db = mock(JdbcTemplate.class);
    private final PlatformTransactionManager manager = mock(PlatformTransactionManager.class);
    private final SpeakerDevices devices = spy(new SpeakerDevices(db, null, manager));
    private final SpeakerVoice voice = mock(SpeakerVoice.class);
    private final SpeakerOperations operations = spy(new SpeakerOperations(db, devices, null, voice));

    @Test
    void ownerIsStableAcrossRebindingAndScopedToTheAccountAndSpeaker() {
        var first = state("device-one", "customer-one", 1, "authorized");
        var same = state("device-one", "customer-one", 4, "authorized");
        var otherAccount = state("device-one", "customer-two", 5, "authorized");
        var otherSpeaker = state("device-two", "customer-one", 1, "authorized");

        String owner = (String) first.get("memo_owner");
        assertTrue(owner.matches("[0-9a-f]{32}"));
        assertEquals(owner, same.get("memo_owner"));
        assertNotEquals(owner, otherAccount.get("memo_owner"));
        assertNotEquals(owner, otherSpeaker.get("memo_owner"));
        assertEquals("ready", first.get("use_status"));
        assertEquals(4L, same.get("binding_version"));
    }

    @Test
    void stateExposesNoMemoOwnerWithoutReadyAccountAccess() {
        assertFalse(state("device-one", null, 3, "unbound").containsKey("memo_owner"));
        assertFalse(state("device-one", "customer-one", 4, "signed_out").containsKey("memo_owner"));
        assertFalse(state("device-one", "customer-one", 4, "authorization_required").containsKey("memo_owner"));
        assertFalse(state("device-one", "customer-one", 4, "admin_disabled").containsKey("memo_owner"));
    }

    private Map<String, Object> state(String deviceId, String customer, long binding, String status) {
        when(manager.getTransaction(any())).thenAnswer(ignored -> new SimpleTransactionStatus());
        var identity = new SpeakerDevices.Identity(deviceId, "fixture");
        var device = new java.util.HashMap<String, Object>();
        device.put("customer_id", customer);
        device.put("binding_version", binding);
        device.put("permission_version", binding);
        device.put("reauthorization_until_ms", 0L);
        doReturn(device).when(devices).load(identity, true);
        doReturn(status).when(operations).useStatus(device);
        when(voice.ticket(device)).thenReturn(Map.of("token", "fixture"));
        if (customer != null) {
            when(db.queryForMap("SELECT display_name FROM speaker_customer WHERE id=?", customer))
                    .thenReturn(Map.of("display_name", "same display name"));
        }
        return operations.state(identity);
    }
}
