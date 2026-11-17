package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.*;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.*;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.datasource.DriverManagerDataSource;
import org.springframework.jdbc.datasource.DataSourceTransactionManager;

/** Scoped integration fixture: only unique speaker records on the approved 104 development database. */
class SpeakerLoginDatabaseTest {
    @TempDir Path directory;
    private JdbcTemplate db;
    private SpeakerDevices devices;
    private SpeakerMarket market;
    private SpeakerCrypto crypto;
    private SpeakerSettings settings;
    private SpeakerGrants grants;
    private SpeakerLogins logins;
    private SpeakerDevices.Identity identity;
    private String login;
    private String device;
    private SpeakerMarket.Tokens tokens;
    private final List<String> extraCustomers=new ArrayList<>();
    @BeforeEach void setup() throws Exception {
        String url=System.getenv("SPRING_DATASOURCE_DRUID_URL");
        if(url==null || !url.startsWith("jdbc:mysql://172.16.18.104:13306/xiaozhi_dev?"))
            throw new IllegalStateException("This test requires the approved Xiaozhi development database");
        var source=new DriverManagerDataSource(url,"xiaozhi_dev",System.getenv("SPRING_DATASOURCE_DRUID_PASSWORD"));
        db=new JdbcTemplate(source); var manager=new DataSourceTransactionManager(source);
        Path key=directory.resolve("key");Files.writeString(key,Base64.getEncoder().encodeToString(new byte[32]));
        Files.setPosixFilePermissions(key,PosixFilePermissions.fromString("rw-------"));
        settings=new SpeakerSettings(true,"https://fixture.example/oidc","fixture-"+UUID.randomUUID(),key.toString(),"","");
        crypto=new SpeakerCrypto(settings);market=mock(SpeakerMarket.class);devices=new SpeakerDevices(db,settings,manager);
        grants=new SpeakerGrants(db,settings,crypto,market,manager);logins=new SpeakerLogins(db,devices,settings,market,crypto,grants);
        device="speaker-test-"+UUID.randomUUID();
        var registered=devices.register(device,"自动化协议夹具","p4-test",0);
        identity=devices.authenticate("Bearer "+registered.get("credential"),device);
        when(market.begin()).thenReturn(new SpeakerMarket.DeviceAuthorization("private-device-code","ABCD-EFGH","https://fixture.example/device?user_code=ABCD-EFGH",600,5));
        login=logins.begin(identity).get("id").toString();
        tokens=new SpeakerMarket.Tokens("fixture-owner-"+UUID.randomUUID(),"access-fixture","refresh-fixture",System.currentTimeMillis()+300000,SpeakerMarket.SCOPES);
        when(market.profile(any())).thenReturn(new ObjectMapper().readTree("{\"sub\":\""+tokens.subject()+"\",\"name\":\"协议测试客户\"}"));
    }
    @AfterEach void cleanup() {
        if(db==null || device==null) return;
        db.update("DELETE FROM speaker_event WHERE device_id=?",device);
        db.update("DELETE FROM speaker_device_operation WHERE device_id=?",device);
        db.update("DELETE FROM speaker_login WHERE device_id=?",device);
        db.update("DELETE FROM speaker_device_state WHERE device_id=?",device);
        db.update("DELETE FROM dc_device_presence WHERE device_id=?",device);
        db.update("DELETE FROM dc_device WHERE device_id=?",device);
        var customers=db.queryForList("SELECT DISTINCT customer_id FROM speaker_grant WHERE client_id=?",settings.clientId());
        db.update("DELETE FROM speaker_grant WHERE client_id=?",settings.clientId());
        for(var customer:customers) db.update("DELETE FROM speaker_customer WHERE id=?",customer.get("customer_id"));
        for(String customer:extraCustomers) db.update("DELETE FROM speaker_customer WHERE id=?",customer);
    }
    private void advance() {
        db.update("UPDATE speaker_login SET next_poll_at_ms=0 WHERE id=?",login);
        logins.advanceDue();
    }
    private void authorize() {
        when(market.poll(anyString())).thenReturn(new SpeakerMarket.Poll(tokens,null));advance();
    }
    @Test void pendingAndSlowDownNeverExposeDeviceCodeOrPollEarly() {
        String result=logins.status(identity,login).toString();
        assertFalse(result.contains("private-device-code"));assertFalse(result.contains("tokens_cipher"));
        logins.advanceDue();verify(market,never()).poll(anyString());
        when(market.poll(anyString())).thenReturn(new SpeakerMarket.Poll(null,"authorization_pending"));advance();
        assertEquals(5000,db.queryForObject("SELECT interval_ms FROM speaker_login WHERE id=?",Long.class,login));
        when(market.poll(anyString())).thenReturn(new SpeakerMarket.Poll(null,"slow_down"));advance();
        assertEquals(10000,db.queryForObject("SELECT interval_ms FROM speaker_login WHERE id=?",Long.class,login));
    }
    @Test void lateAuthorizationCannotRebindChangedDeviceGeneration() {
        authorize();
        db.update("UPDATE speaker_device_state SET binding_version=binding_version+1 WHERE device_id=?",device);
        when(market.credential(any())).thenReturn(new SpeakerMarket.Credential("key-fixture","https://fixture.example",null));
        advance();
        assertEquals("cancelled",logins.status(identity,login).get("status"));
        assertNull(db.queryForMap("SELECT customer_id FROM speaker_device_state WHERE device_id=?",device).get("customer_id"));
    }
    @Test void provisioningFailureRetainsAuthorizationAndRetriesWithoutAnotherScan() {
        authorize();when(market.credential(any())).thenThrow(new SpeakerFailure(503,"MARKET_UNAVAILABLE"));advance();
        assertEquals("authorized",logins.status(identity,login).get("status"));
        var grant=db.queryForMap("SELECT * FROM speaker_grant WHERE client_id=?",settings.clientId());
        assertEquals("refresh-fixture",crypto.decrypt(grant.get("refresh_cipher").toString()));
        doReturn(new SpeakerMarket.Credential("key-fixture","https://fixture.example",null)).when(market).credential(any());advance();
        assertEquals("completed",logins.status(identity,login).get("status"));
        assertEquals(true,db.queryForMap("SELECT use_enabled FROM speaker_device_state WHERE device_id=?",device).get("use_enabled"));
        verify(market,times(1)).poll(anyString());
    }
    @Test void refreshFailureDoesNotRevokeKeyOrDisableDevice() {
        authorize();when(market.credential(any())).thenReturn(new SpeakerMarket.Credential("key-fixture","https://fixture.example",null));advance();
        var row=db.queryForMap("SELECT * FROM speaker_grant WHERE client_id=?",settings.clientId());
        row.put("subject",tokens.subject());
        when(market.refresh(anyString(),anyString())).thenThrow(new SpeakerFailure(503,"MARKET_UNAVAILABLE"));
        grants.refresh(row);
        var saved=db.queryForMap("SELECT * FROM speaker_grant WHERE client_id=?",settings.clientId());
        assertEquals("uncertain",saved.get("token_status"));assertTrue(SpeakerGrants.usable(saved,System.currentTimeMillis()));
        assertEquals(true,db.queryForMap("SELECT use_enabled FROM speaker_device_state WHERE device_id=?",device).get("use_enabled"));
    }
    private void bind() {
        authorize();when(market.credential(any())).thenReturn(new SpeakerMarket.Credential("key-fixture","https://fixture.example",null));advance();
    }
    @Test void signOutAndUnbindAreIdempotentAndKeepOtherDevicesAndApplicationKey() {
        bind();var operations=new SpeakerOperations(db,devices,grants,mock(SpeakerVoice.class));
        String customer=db.queryForObject("SELECT customer_id FROM speaker_device_state WHERE device_id=?",String.class,device);
        String second="speaker-test-"+UUID.randomUUID();
        devices.register(second,"第二台协议夹具","p4-test",0);
        try {
            db.update("UPDATE speaker_device_state SET customer_id=?,use_enabled=TRUE WHERE device_id=?",customer,second);
            long version=((Number)operations.state(identity).get("permission_version")).longValue();
            operations.action(identity,"sign-out",version,"exit-one","device");
            operations.action(identity,"sign-out",version,"exit-one","device");
            assertEquals(customer,db.queryForObject("SELECT customer_id FROM speaker_device_state WHERE device_id=?",String.class,device));
            assertEquals(true,db.queryForMap("SELECT use_enabled FROM speaker_device_state WHERE device_id=?",second).get("use_enabled"));
            operations.action(identity,"unbind",version+1,"unbind-one","device");
            assertNull(db.queryForMap("SELECT customer_id FROM speaker_device_state WHERE device_id=?",device).get("customer_id"));
            assertEquals(customer,db.queryForObject("SELECT customer_id FROM speaker_device_state WHERE device_id=?",String.class,second));
            assertTrue(SpeakerGrants.usable(grants.grant(customer),System.currentTimeMillis()));
            verify(market,never()).refresh(anyString(),anyString());
        } finally {
            db.update("DELETE FROM speaker_event WHERE device_id=?",second);
            db.update("DELETE FROM speaker_device_state WHERE device_id=?",second);
            db.update("DELETE FROM dc_device WHERE device_id=?",second);
        }
    }
    @Test void differentAccountCannotReplaceCurrentOwner() {
        String owner=UUID.randomUUID().toString();extraCustomers.add(owner);
        db.update("INSERT INTO speaker_customer(id,issuer,subject,created_at_ms) VALUES (?,?,?,?)",owner,settings.issuer(),"other-"+UUID.randomUUID(),System.currentTimeMillis());
        db.update("UPDATE speaker_device_state SET customer_id=? WHERE device_id=?",owner,device);
        bind();
        assertEquals("denied",logins.status(identity,login).get("status"));
        assertEquals("OWNER_REQUIRED",logins.status(identity,login).get("error_code"));
        assertEquals(owner,db.queryForObject("SELECT customer_id FROM speaker_device_state WHERE device_id=?",String.class,device));
    }
    @Test void enablingAdministrationDoesNotResumeUserSignedOutDevice() {
        bind();var operations=new SpeakerOperations(db,devices,grants,mock(SpeakerVoice.class));
        long version=((Number)operations.state(identity).get("permission_version")).longValue();
        operations.action(identity,"sign-out",version,"exit-one","device");
        operations.adminAction(device,"disable",version+1,"disable-one",0);
        operations.adminAction(device,"enable",version+2,"enable-one",0);
        assertEquals("signed_out",operations.state(identity).get("use_status"));
    }
    @Test void customerSummaryUsesRealBindingAndHeartbeatAndHidesSecrets() {
        bind();devices.heartbeat(identity,"p4-test","fixture-version");
        var customers=new SpeakerCustomers(db,settings,grants,market);
        String customer=db.queryForObject("SELECT customer_id FROM speaker_device_state WHERE device_id=?",String.class,device);
        String result=customers.detail(customer).toString();
        assertTrue(result.contains("online=true"));assertFalse(result.contains("cipher"));assertFalse(result.contains("key-fixture"));
        assertFalse(result.contains("access-fixture"));assertFalse(result.contains("refresh-fixture"));
        var summary=(Map<?,?>)customers.list(tokens.subject(),1,50);
        var row=(Map<?,?>)((List<?>)summary.get("items")).getFirst();
        assertEquals(1,((Number)row.get("device_count")).intValue());assertEquals(1,((Number)row.get("online_count")).intValue());
    }
}
