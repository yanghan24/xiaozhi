package xiaozhi.modules.model.market;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import java.util.*;
import java.util.concurrent.*;
import org.junit.jupiter.api.Test;
import xiaozhi.modules.speaker.*;

class MarketResourceCredentialsTest {
    SpeakerGrants grants=mock(SpeakerGrants.class);
    SpeakerMarket market=mock(SpeakerMarket.class);
    SpeakerCrypto crypto=mock(SpeakerCrypto.class);
    MarketResourceCredentials service=new MarketResourceCredentials(grants,market,crypto,
            new SpeakerSettings(true,"https://aimarket.hncig.cn/oidc","device-client","",""));
    Map<String,Object> grant() {
        return new HashMap<>(Map.of("token_status","active","credential_status","active","credential_cipher","cipher",
                "credential_base","https://aimarket.hncig.cn","generation","old-generation",
                "credential_verified_at_ms",System.currentTimeMillis()));
    }
    @Test void validResourceKeyDoesNotRefreshOrProvisionOnEveryConnection() {
        when(grants.grant("owner")).thenReturn(grant());when(crypto.decrypt("cipher")).thenReturn("owner-key");
        assertEquals("owner-key",service.forCustomer("owner").key());assertEquals("owner-key",service.forCustomer("owner").key());
        verifyNoInteractions(market);verify(grants,never()).currentTokens(anyString());
    }
    private void prepareStore(Map<String,Object> row, SpeakerMarket.Credential credential) {
        when(grants.grant("owner")).thenReturn(row);
        var tokens=new SpeakerMarket.Tokens("subject","access","refresh",System.currentTimeMillis()+300000,"credentials:manage");
        when(grants.currentTokens("owner")).thenReturn(tokens);
        when(market.credential(tokens)).thenReturn(credential);
        when(grants.claimCredentialCheck(eq("owner"),eq("old-generation"),anyLong(),anyLong())).thenReturn(true);
        when(grants.saveVerifiedCredential(eq("owner"),eq("old-generation"),anyLong(),eq(credential),anyBoolean()))
                .thenAnswer(call->{
                    row.put("credential_cipher","encrypted");
                    row.put("credential_status","active");
                    row.put("credential_verified_at_ms",System.currentTimeMillis());
                    if(call.getArgument(4,Boolean.class)) row.put("generation","new-generation");
                    return true;
                });
        when(crypto.decrypt("cipher")).thenReturn("old-key");
        when(crypto.decrypt("encrypted")).thenReturn(credential.key());
    }
    @Test void pendingReauthorizationReissuesAndVerifiesBeforeSharingWithConcurrentCallers() throws Exception {
        var row=new ConcurrentHashMap<>(grant());
        row.put("credential_status","pending");
        row.put("credential_verified_at_ms",0L);
        var replacement=new SpeakerMarket.Credential("new-key","https://aimarket.hncig.cn",null);
        prepareStore(row,replacement);
        when(market.credentialAccepted(replacement)).thenReturn(true);
        try(var executor=Executors.newFixedThreadPool(2)) {
            var first=executor.submit(()->service.forCustomer("owner").key());
            var second=executor.submit(()->service.forCustomer("owner").key());
            assertEquals("new-key",first.get(5,TimeUnit.SECONDS));
            assertEquals("new-key",second.get(5,TimeUnit.SECONDS));
        }
        verify(market,times(1)).credential(any());
        verify(market,times(1)).credentialAccepted(replacement);
        verify(crypto,never()).decrypt("cipher");
    }
    @Test void legacyActiveKeyIsCheckedBeforeUseWithoutNeedlessReissue() {
        var row=grant();
        row.put("credential_verified_at_ms",0L);
        var original=new SpeakerMarket.Credential("old-key","https://aimarket.hncig.cn",null);
        prepareStore(row,original);
        when(market.credentialAccepted(original)).thenReturn(true);
        assertEquals("old-key",service.forCustomer("owner").key());
        assertEquals("old-key",service.forCustomer("owner").key());
        verify(market,times(1)).credentialAccepted(original);
        verify(market,never()).credential(any());
    }
    @Test void sameRejectedKeyStopsAfterOneRecoveryAndNeverReachesProviders() {
        var row=grant();
        var original=new SpeakerMarket.Credential("old-key","https://aimarket.hncig.cn",null);
        prepareStore(row,original);
        when(grants.markCredentialRejected(eq("owner"),eq("old-generation"),anyLong(),anyBoolean()))
                .thenAnswer(call->{
                    row.put("credential_status",call.getArgument(3,Boolean.class)?"blocked":"rejected");
                    row.put("credential_verified_at_ms",0L);
                    return true;
                });
        assertEquals("MARKET_MODEL_KEY_REJECTED",
                assertThrows(SpeakerFailure.class,()->service.current("owner","old-generation")).getMessage());
        assertThrows(SpeakerFailure.class,()->service.forCustomer("owner"));
        verify(market,times(1)).credential(any());
        verify(market,times(2)).credentialAccepted(original);
        verify(grants,never()).saveVerifiedCredential(anyString(),anyString(),anyLong(),any(),anyBoolean());
    }
    @Test void checkOutageRetainsPreviouslyVerifiedKeyWithoutReissuing() {
        var row=grant();
        row.put("credential_verified_at_ms",System.currentTimeMillis()-MarketResourceCredentials.VERIFICATION_MS-1);
        var original=new SpeakerMarket.Credential("old-key","https://aimarket.hncig.cn",null);
        prepareStore(row,original);
        when(market.credentialAccepted(original)).thenThrow(new SpeakerFailure(503,"MARKET_CREDENTIAL_UNAVAILABLE"));
        assertEquals("old-key",service.forCustomer("owner").key());
        verify(market,never()).credential(any());
        verify(grants,never()).markCredentialRejected(anyString(),anyString(),anyLong(),anyBoolean());
    }
    @Test void rejectsRevokedIdentityOrForeignCredentialAuthority() {
        var row=grant();row.put("token_status","reauth_required");when(grants.grant("owner")).thenReturn(row);
        assertThrows(SpeakerFailure.class,()->service.forCustomer("owner"));
        row.put("token_status","active");row.put("credential_base","https://other.example");
        assertThrows(SpeakerFailure.class,()->service.forCustomer("owner"));verifyNoInteractions(market);
    }
}
