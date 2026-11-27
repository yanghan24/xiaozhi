package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

import java.time.Duration;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.data.redis.core.ValueOperations;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;

class SpeakerRechargeTest {
    private static final String ID = "50aa355f-f915-4289-bd9e-9909db1aff93";
    private static final String KEY = "speaker:recharge:" + ID;
    private final SpeakerDevices devices = mock(SpeakerDevices.class);
    private final SpeakerOperations operations = mock(SpeakerOperations.class);
    private final SpeakerGrants grants = mock(SpeakerGrants.class);
    private final SpeakerLogins logins = mock(SpeakerLogins.class);
    private final SpeakerMarket market = mock(SpeakerMarket.class);
    private final StringRedisTemplate redis = mock(StringRedisTemplate.class);
    private final SpeakerDevices.Identity device = new SpeakerDevices.Identity("speaker-one", "credential-fixture");
    private final SpeakerMarket.Tokens tokens = new SpeakerMarket.Tokens("subject", "access", "refresh",
            Long.MAX_VALUE, SpeakerMarket.RECHARGE_SCOPE);
    private final SpeakerRecharge recharge = new SpeakerRecharge(devices, operations, grants, logins, market, redis);

    @SuppressWarnings("unchecked")
    private ValueOperations<String,String> storage() {
        var storage = (ValueOperations<String,String>) mock(ValueOperations.class);
        when(redis.opsForValue()).thenReturn(storage);
        return storage;
    }

    private void authorizeDevice() {
        var owner = Map.<String,Object>of("customer_id","customer","binding_version",5L,"permission_version",9L);
        when(devices.load(device,false)).thenReturn(owner);
        when(operations.useStatus(owner)).thenReturn("authorized");
    }

    @Test void asksForExplicitConsentBeforeTheFirstRecharge() {
        authorizeDevice();
        when(grants.grant("customer")).thenReturn(Map.of("token_scope",SpeakerMarket.SCOPES));
        assertEquals("RECHARGE_AUTHORIZATION_REQUIRED",assertThrows(SpeakerFailure.class,()->recharge.open(device)).getMessage());
        verifyNoInteractions(market);
        verifyNoInteractions(logins);
        recharge.authorize(device);
        verify(logins).beginRecharge(device);
    }

    @Test void storesOnlyEntryOwnershipAndRejectsADifferentBinding() {
        authorizeDevice();
        var storage = storage();
        when(grants.grant("customer")).thenReturn(Map.of("token_scope",SpeakerMarket.RECHARGE_SCOPE));
        when(grants.currentTokens("customer")).thenReturn(tokens);
        when(market.rechargeEntry(tokens)).thenReturn(new SpeakerMarket.RechargeEntry(ID,"https://fixture/#ticket=secret",300,2));
        assertEquals(ID,recharge.open(device).entry_id());
        verify(storage).set(KEY,"speaker-one|customer|5|9",Duration.ofHours(24));
        when(storage.get(KEY)).thenReturn("speaker-one|customer|4|9");
        assertEquals(404,assertThrows(SpeakerFailure.class,()->recharge.status(device,ID)).status);
        verify(market,never()).rechargeStatus(anyString(),any());
    }

    @Test void refreshesBalanceOnlyAfterTheMarketReportsPayment() {
        authorizeDevice();
        var storage = storage();
        when(storage.get(KEY)).thenReturn("speaker-one|customer|5|9");
        when(storage.setIfAbsent(KEY+":balance","1",Duration.ofSeconds(15))).thenReturn(true,false);
        when(grants.grant("customer")).thenReturn(Map.of("token_scope",SpeakerMarket.RECHARGE_SCOPE));
        when(grants.currentTokens("customer")).thenReturn(tokens);
        when(market.rechargeStatus(ID,tokens)).thenReturn("pending","paid","paid");
        when(grants.deviceBalance("customer")).thenReturn("129.5");
        assertEquals("pending",recharge.status(device,ID).status());
        verify(grants,never()).refreshBalance("customer");
        assertEquals("129.5",recharge.status(device,ID).balance());
        assertEquals("paid",recharge.status(device,ID).status());
        verify(grants,times(1)).refreshBalance("customer");
    }

    @Test void deviceEndpointRejectsMissingCredentialsAndReturnsScopeFailureWithoutCaching() throws Exception {
        var http=MockMvcBuilders.standaloneSetup(new SpeakerRechargeController(devices,recharge))
                .setControllerAdvice(new SpeakerErrors(),new SpeakerProtocolResponse()).build();
        String route="/speaker/v1/account/recharge/entries";
        when(devices.authenticate(null,null)).thenThrow(new SpeakerFailure(401,"INVALID_DEVICE_CREDENTIAL"));
        http.perform(post(route)).andExpect(status().isUnauthorized());
        authorizeDevice();
        when(devices.authenticate("Bearer fixture","speaker-one")).thenReturn(device);
        when(grants.grant("customer")).thenReturn(Map.of("token_scope",SpeakerMarket.SCOPES));
        http.perform(post(route).header("Authorization","Bearer fixture").header("Device-Id","speaker-one"))
                .andExpect(status().isConflict())
                .andExpect(header().string("Cache-Control","no-store"))
                .andExpect(jsonPath("$.code").value("RECHARGE_AUTHORIZATION_REQUIRED"));
    }
}
