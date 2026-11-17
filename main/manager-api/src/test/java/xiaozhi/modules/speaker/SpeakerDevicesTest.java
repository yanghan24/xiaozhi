package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.transaction.PlatformTransactionManager;

class SpeakerDevicesTest {
    private final JdbcTemplate db = mock(JdbcTemplate.class);
    private final SpeakerDevices devices = new SpeakerDevices(db,
            new SpeakerSettings(true,"https://aimarket.hncig.cn/oidc","","","",""),
            mock(PlatformTransactionManager.class));
    @Test void rejectsSelfReportedIdentityAndInvalidCredentials() {
        var failure = assertThrows(SpeakerFailure.class, () -> devices.authenticate("Bearer short","device-a"));
        assertEquals(401,failure.status); verifyNoInteractions(db);
        when(db.queryForList(anyString(),any(Object[].class))).thenReturn(List.of());
        assertThrows(SpeakerFailure.class, () -> devices.authenticate("Bearer "+"a".repeat(43),"device-a"));
    }
    @Test void checksAdmissionAfterMatchingDeviceAndCredential() {
        when(db.queryForList(anyString(),any(Object[].class))).thenReturn(List.of(Map.of("admitted",false)));
        assertEquals(403,assertThrows(SpeakerFailure.class,
                () -> devices.authenticate("Bearer "+"a".repeat(43),"device-a")).status);
    }
    @Test void presenceExpiresWithoutChangingOwnership() {
        assertFalse(SpeakerSupport.online(null,100_000));
        assertTrue(SpeakerSupport.online(99_999L,100_000));
        assertFalse(SpeakerSupport.online(25_000L,100_000));
        assertFalse(SpeakerSupport.online(100_001L,100_000));
    }
    @Test void validHeartbeatUsesAuthenticatedDeviceAndPreservesBinding() {
        when(db.queryForList(anyString(),any(Object[].class))).thenReturn(List.of(Map.of("admitted",true)));
        var identity=devices.authenticate("Bearer "+"a".repeat(43),"device-a");
        assertEquals("device-a",devices.heartbeat(identity,"p4","2.2.6").get("device_id"));
        verify(db).update(startsWith("UPDATE speaker_device_state SET last_seen_at_ms="),
                anyLong(),eq("p4"),eq("2.2.6"),eq("device-a"));
        verify(db,never()).update(contains("customer_id="),any(Object[].class));
    }
}
