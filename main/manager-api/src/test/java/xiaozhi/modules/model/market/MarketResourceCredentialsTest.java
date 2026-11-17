package xiaozhi.modules.model.market;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import java.util.*;
import org.junit.jupiter.api.Test;
import xiaozhi.modules.speaker.*;

class MarketResourceCredentialsTest {
    SpeakerGrants grants=mock(SpeakerGrants.class);
    SpeakerMarket market=mock(SpeakerMarket.class);
    SpeakerCrypto crypto=mock(SpeakerCrypto.class);
    MarketResourceCredentials service=new MarketResourceCredentials(grants,market,crypto,
            new SpeakerSettings(true,"https://aimarket.hncig.cn/oidc","device-client","","",""));
    Map<String,Object> grant() {
        return new HashMap<>(Map.of("token_status","active","credential_status","active","credential_cipher","cipher",
                "credential_base","https://aimarket.hncig.cn"));
    }
    @Test void validResourceKeyDoesNotRefreshOrProvisionOnEveryConnection() {
        when(grants.grant("owner")).thenReturn(grant());when(crypto.decrypt("cipher")).thenReturn("owner-key");
        assertEquals("owner-key",service.forCustomer("owner").key());assertEquals("owner-key",service.forCustomer("owner").key());
        verifyNoInteractions(market);verify(grants,never()).currentTokens(anyString());
    }
    @Test void lazilyProvisionsAndPersistsThroughExistingEncryptedStore() {
        var row=grant();row.remove("credential_cipher");when(grants.grant("owner")).thenReturn(row);
        var tokens=new SpeakerMarket.Tokens("subject","access","refresh",System.currentTimeMillis()+300000,"credentials:manage");
        when(grants.currentTokens("owner")).thenReturn(tokens);
        var credential=new SpeakerMarket.Credential("owner-key","https://aimarket.hncig.cn",null);
        when(market.credential(tokens)).thenReturn(credential);
        doAnswer(call->{row.put("credential_cipher","encrypted");return null;}).when(grants).saveCredential("owner",tokens,credential);
        when(crypto.decrypt("encrypted")).thenReturn("owner-key");
        assertEquals("owner-key",service.forCustomer("owner").key());
        verify(grants).saveCredential("owner",tokens,credential);
    }
    @Test void rejectsRevokedIdentityOrForeignCredentialAuthority() {
        var row=grant();row.put("token_status","reauth_required");when(grants.grant("owner")).thenReturn(row);
        assertThrows(SpeakerFailure.class,()->service.forCustomer("owner"));
        row.put("token_status","active");row.put("credential_base","https://other.example");
        assertThrows(SpeakerFailure.class,()->service.forCustomer("owner"));verifyNoInteractions(market);
    }
}
