package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;
import java.math.BigDecimal;
import java.net.URI;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.*;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.*;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.dao.TransientDataAccessResourceException;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.datasource.DriverManagerDataSource;
import org.springframework.jdbc.datasource.DataSourceTransactionManager;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;

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
    private String authorization;
    private SpeakerMarket.Tokens tokens;
    private final List<String> extraCustomers=new ArrayList<>();
    private final List<String> enrollmentDevices = new ArrayList<>();
    @BeforeEach void setup() throws Exception {
        String url=System.getenv("SPRING_DATASOURCE_DRUID_URL");
        if(url==null || !url.startsWith("jdbc:mysql://172.16.18.104:13306/xiaozhi_dev?"))
            throw new IllegalStateException("This test requires the approved Xiaozhi development database");
        var source=new DriverManagerDataSource(url,"xiaozhi_dev",System.getenv("SPRING_DATASOURCE_DRUID_PASSWORD"));
        db=new JdbcTemplate(source); var manager=new DataSourceTransactionManager(source);
        Path key=directory.resolve("key");Files.writeString(key,Base64.getEncoder().encodeToString(new byte[32]));
        Files.setPosixFilePermissions(key,PosixFilePermissions.fromString("rw-------"));
        settings=new SpeakerSettings(true,"https://fixture.example/oidc","fixture-"+UUID.randomUUID(),key.toString(),"");
        crypto=new SpeakerCrypto(settings);market=mock(SpeakerMarket.class);devices=new SpeakerDevices(db,settings,manager);
        grants=new SpeakerGrants(db,settings,crypto,market,manager);logins=new SpeakerLogins(db,devices,settings,market,crypto,grants);
        device="speaker-test-"+UUID.randomUUID();
        var registered=devices.register(device,"自动化协议夹具","p4-test",0);
        authorization="Bearer "+registered.get("credential");
        identity=devices.authenticate(authorization,device);
        when(market.begin()).thenReturn(new SpeakerMarket.DeviceAuthorization("private-device-code","ABCD-EFGH","https://fixture.example/device?user_code=ABCD-EFGH",600,5));
        login=logins.begin(identity).get("id").toString();
        tokens=new SpeakerMarket.Tokens("fixture-owner-"+UUID.randomUUID(),"access-fixture","refresh-fixture",System.currentTimeMillis()+300000,SpeakerMarket.SCOPES);
        when(market.profile(any())).thenReturn(new ObjectMapper().readTree("{\"sub\":\""+tokens.subject()+"\",\"name\":\"协议测试客户\"}"));
    }
    @AfterEach void cleanup() {
        if(db==null || device==null) return;
        removeDevice(device);
        enrollmentDevices.forEach(this::removeDevice);
        var customers=db.queryForList("SELECT DISTINCT customer_id FROM speaker_grant WHERE client_id=?",settings.clientId());
        db.update("DELETE FROM speaker_grant WHERE client_id=?",settings.clientId());
        for(var customer:customers) db.update("DELETE FROM speaker_customer WHERE id=?",customer.get("customer_id"));
        for(String customer:extraCustomers) db.update("DELETE FROM speaker_customer WHERE id=?",customer);
    }

    private void removeDevice(String id) {
        db.update("DELETE FROM speaker_event WHERE device_id=?", id);
        db.update("DELETE FROM speaker_device_operation WHERE device_id=?", id);
        db.update("DELETE FROM speaker_login WHERE device_id=?", id);
        db.update("DELETE FROM speaker_device_state WHERE device_id=?", id);
        db.update("DELETE FROM dc_device_presence WHERE device_id=?", id);
        db.update("DELETE FROM dc_device WHERE device_id=?", id);
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
    @Test void expiredLoginHidesItsCodeAndTheNextAttemptGetsANewOne() {
        assertEquals("ABCD-EFGH",logins.status(identity,login).get("user_code"));
        db.update("UPDATE speaker_login SET expires_at_ms=1 WHERE id=?",login);
        var expired=logins.status(identity,login);
        assertEquals("expired",expired.get("status"));
        assertEquals("LOGIN_EXPIRED",expired.get("error_code"));
        assertNull(expired.get("verification_uri"));
        assertNull(expired.get("user_code"));

        logins.advanceDue();
        assertNull(db.queryForMap("SELECT user_code FROM speaker_login WHERE id=?",login).get("user_code"));
        db.update("UPDATE speaker_login SET verification_uri=?,user_code=? WHERE id=?",
                "https://fixture.example/device?user_code=ABCD-EFGH","ABCD-EFGH",login);
        assertNull(logins.status(identity,login).get("user_code"));

        when(market.begin()).thenReturn(new SpeakerMarket.DeviceAuthorization(
                "second-private-device-code","JKLM-NPQR","https://fixture.example/device?user_code=JKLM-NPQR",600,5));
        var next=logins.begin(identity);
        assertNotEquals(login,next.get("id"));
        assertEquals("JKLM-NPQR",next.get("user_code"));
    }
    @Test void acceptedConsentHidesTheCodeWhileBindingCompletes() {
        authorize();
        var authorized=logins.status(identity,login);
        assertEquals("authorized",authorized.get("status"));
        assertNull(authorized.get("verification_uri"));
        assertNull(authorized.get("user_code"));
        assertEquals("authorized",logins.refresh(identity,login).get("status"));

        advance();
        assertEquals("completed",logins.status(identity,login).get("status"));
        assertEquals("completed",logins.refresh(identity,login).get("status"));
        verify(market,times(1)).begin();
    }
    @Test void scanIsOneTimeAndRefreshingReplacesTheRequestForThisDevice() throws Exception {
        var http=MockMvcBuilders.standaloneSetup(new SpeakerDeviceController(devices,logins,null,null))
                .setControllerAdvice(new SpeakerErrors(),new SpeakerProtocolResponse()).build();
        String route="/speaker/v1";
        var initial=logins.status(identity,login);
        String scan=route+initial.get("scan_path");
        String providerUrl=initial.get("verification_uri").toString();
        when(market.verificationUri(providerUrl)).thenReturn(URI.create(providerUrl));

        http.perform(head(scan)).andExpect(status().isNoContent());
        http.perform(get(scan).header("Sec-Purpose","prefetch")).andExpect(status().isNoContent());
        assertEquals(false,logins.status(identity,login).get("scanned"));
        http.perform(get(scan)).andExpect(status().isSeeOther())
                .andExpect(header().string("Location",providerUrl))
                .andExpect(header().string("Cache-Control","no-store"));
        http.perform(get(scan)).andExpect(status().isGone());
        var scanned=logins.begin(identity,true);
        assertEquals(login,scanned.get("id"));
        assertEquals(true,scanned.get("scanned"));
        assertNull(scanned.get("scan_path"));
        assertEquals("ABCD-EFGH",scanned.get("user_code"));
        assertNull(customerId());
        verify(market,never()).poll(anyString());

        String refresh=route+"/login/"+login+"/refresh";
        http.perform(post(refresh)).andExpect(status().isUnauthorized());
        when(market.begin()).thenReturn(new SpeakerMarket.DeviceAuthorization(
                "second-private-device-code","JKLM-NPQR","https://fixture.example/device?user_code=JKLM-NPQR",600,5));
        http.perform(post(refresh).header("Authorization",authorization).header("Device-Id",device))
                .andExpect(status().isOk()).andExpect(jsonPath("$.scanned").value(false))
                .andExpect(jsonPath("$.user_code").value("JKLM-NPQR"));
        var replacement=logins.refresh(identity,login);
        assertNotEquals(login,replacement.get("id"));
        assertEquals("cancelled",logins.status(identity,login).get("status"));
        http.perform(get(scan)).andExpect(status().isGone());
        verify(market,times(2)).begin();
    }

    @Test void resumingAnExpiredLoginWaitsForExplicitRefresh() {
        db.update("UPDATE speaker_login SET expires_at_ms=1 WHERE id=?",login);
        assertEquals("expired",logins.begin(identity,true).get("status"));
        verify(market,times(1)).begin();

        assertNotEquals(login,logins.refresh(identity,login).get("id"));
        verify(market,times(2)).begin();
    }
    @Test void lateAuthorizationCannotRebindChangedDeviceGeneration() {
        authorize();
        var profile=new ObjectMapper().createObjectNode().put("sub",tokens.subject());
        when(market.profile(any())).thenAnswer(call -> {
            db.update("UPDATE speaker_device_state SET binding_version=binding_version+1 WHERE device_id=?",device);
            return profile;
        });
        when(market.credential(any())).thenReturn(new SpeakerMarket.Credential("key-fixture","https://fixture.example",null));
        advance();
        assertEquals("cancelled",logins.status(identity,login).get("status"));
        assertNull(db.queryForMap("SELECT customer_id FROM speaker_device_state WHERE device_id=?",device).get("customer_id"));
        verify(market,never()).credential(any());
        assertEquals(0,db.queryForObject("SELECT COUNT(*) FROM speaker_grant WHERE client_id=?",Integer.class,settings.clientId()));
    }
    @Test void identityBindingCompletesWithoutRequestingAResourceKey() {
        authorize();advance();
        assertEquals("completed",logins.status(identity,login).get("status"));
        var grant=db.queryForMap("SELECT * FROM speaker_grant WHERE client_id=?",settings.clientId());
        assertEquals("refresh-fixture",crypto.decrypt(grant.get("refresh_cipher").toString()));
        assertNull(grant.get("credential_cipher"));
        assertEquals(true,db.queryForMap("SELECT use_enabled FROM speaker_device_state WHERE device_id=?",device).get("use_enabled"));
        verify(market,times(1)).poll(anyString());
        verify(market,never()).credential(any());
    }

    @Test void freshSpeakerCanEnrollScanWithRechargeScopeAndRecoverTheCredentialUntilAcknowledged() {
        var enrollment = new SpeakerEnrollment(db, devices, crypto);
        String id = "enr-" + UUID.randomUUID().toString().replace("-", "");
        enrollmentDevices.add(id);
        String proof = "Bearer " + SpeakerSupport.token();
        var pending = enrollment.beginOrClaim(proof, id, "p4-test", "2.6.2");
        assertEquals(Map.of("device_id", id, "status", "pending"), pending);
        assertEquals(pending, enrollment.beginOrClaim(proof, id, "p4-test", "2.6.2"));
        assertEquals(403, assertThrows(SpeakerFailure.class, () -> devices.authenticate(proof, id)).status);
        assertEquals(403, assertThrows(SpeakerFailure.class,
                () -> enrollment.beginOrClaim("Bearer " + SpeakerSupport.token(), id, "p4-test", "2.6.2")).status);
        assertEquals(1, db.queryForObject("SELECT COUNT(*) FROM speaker_event WHERE device_id=? AND action='registered'", Integer.class, id));

        var loginIdentity = devices.authenticateForLogin(proof, id);
        login = logins.begin(loginIdentity).get("id").toString();
        tokens = new SpeakerMarket.Tokens(tokens.subject(),tokens.access(),tokens.refresh(),tokens.expiresAt(),
                tokens.scope()+" "+SpeakerMarket.RECHARGE_SCOPE);
        authorize();
        advance();
        assertEquals("completed", logins.status(loginIdentity, login).get("status"));
        verify(market,never()).beginRecharge();
        assertEquals(403, assertThrows(SpeakerFailure.class, () -> devices.authenticate(proof, id)).status);

        var issued = enrollment.beginOrClaim(proof, id, "p4-test", "2.6.2");
        assertEquals("issued", issued.get("status"));
        assertEquals(issued, enrollment.beginOrClaim(proof, id, "p4-test", "2.6.2"));
        assertEquals(401, assertThrows(SpeakerFailure.class, () -> devices.authenticateForLogin(proof, id)).status);
        var assigned = devices.authenticate("Bearer " + issued.get("credential"), id);
        var customer = devices.load(assigned, false).get("customer_id").toString();
        assertTrue(grants.currentTokens(customer).allowsRecharge());
        assertEquals(id, devices.heartbeat(assigned, "p4-test", "2.6.2").get("device_id"));
        enrollment.acknowledge(assigned);
        enrollment.acknowledge(assigned);
        assertEquals(403, assertThrows(SpeakerFailure.class,
                () -> enrollment.beginOrClaim(proof, id, "p4-test", "2.6.2")).status);
        assertEquals(assigned, devices.authenticate("Bearer " + issued.get("credential"), id));
    }
    @Test void refreshFailureDoesNotRevokeKeyOrDisableDevice() {
        authorize();when(market.credential(any())).thenReturn(new SpeakerMarket.Credential("key-fixture","https://fixture.example",null));advance();
        db.update("UPDATE speaker_grant SET access_expires_at_ms=0 WHERE client_id=?",settings.clientId());
        var row=db.queryForMap("SELECT * FROM speaker_grant WHERE client_id=?",settings.clientId());
        row.put("subject",tokens.subject());
        when(market.refresh(anyString(),anyString())).thenThrow(new SpeakerFailure(503,"MARKET_UNAVAILABLE"));
        grants.refresh(row);
        var saved=db.queryForMap("SELECT * FROM speaker_grant WHERE client_id=?",settings.clientId());
        assertEquals("uncertain",saved.get("token_status"));assertTrue(SpeakerGrants.usable(saved,System.currentTimeMillis()));
        assertEquals(true,db.queryForMap("SELECT use_enabled FROM speaker_device_state WHERE device_id=?",device).get("use_enabled"));
        grants.refreshDue();
        verify(market,times(1)).refresh(anyString(),anyString());
    }

    @Test void scheduledRetryRetainsTheTokenAndRecoversBalanceFromAnotherInstance() {
        bind();
        String customer=customerId();
        db.update("UPDATE speaker_grant SET access_expires_at_ms=0 WHERE customer_id=?",customer);
        var before=grants.grant(customer);
        var next=replacementTokens();
        when(market.refresh(tokens.refresh(),tokens.subject()))
                .thenThrow(new SpeakerMarket.RefreshRetry("MARKET_REFRESH_NOT_SENT",0)).thenReturn(next);
        grants.refreshDue();
        var scheduled=grants.grant(customer);
        assertEquals("active",scheduled.get("token_status"));
        assertEquals(before.get("refresh_cipher"),scheduled.get("refresh_cipher"));
        assertEquals(2_000,SpeakerSupport.number(scheduled,"token_retry_at_ms")-SpeakerSupport.number(scheduled,"token_updated_at_ms"));
        var other=grantsUsing(db);
        other.refreshDue();
        verify(market,times(1)).refresh(anyString(),anyString());
        db.update("UPDATE speaker_grant SET token_retry_at_ms=0 WHERE customer_id=?",customer);
        other.refreshDue();
        var saved=grants.grant(customer);
        assertEquals(SpeakerSupport.number(before,"token_version")+1,SpeakerSupport.number(saved,"token_version"));
        assertEquals(next.refresh(),grants.currentTokens(customer).refresh());
        // The scope is not persisted; only the issued access token matters for the query.
        when(market.balance(argThat(value -> value!=null && next.access().equals(value.access())))).thenReturn(new BigDecimal("42.5"));
        assertEquals("42.5",other.deviceBalance(customer));
    }

    @Test void rotationHasOneInFlightRequestAcrossInstances() throws Exception {
        bind();
        String customer=customerId();
        db.update("UPDATE speaker_grant SET access_expires_at_ms=0 WHERE customer_id=?",customer);
        var called=new CountDownLatch(1);
        var release=new CountDownLatch(1);
        when(market.refresh(anyString(),anyString())).thenAnswer(call -> {
            called.countDown();
            if (!release.await(10,TimeUnit.SECONDS)) throw new IllegalStateException("Fixture did not release refresh");
            return replacementTokens();
        });
        try (var executor=Executors.newSingleThreadExecutor()) {
            var first=executor.submit(grants::refreshDue);
            try {
                assertTrue(called.await(10,TimeUnit.SECONDS));
                grantsUsing(db).refreshDue();
                verify(market,times(1)).refresh(anyString(),anyString());
            } finally {
                release.countDown();
            }
            first.get(10,TimeUnit.SECONDS);
        }
        assertEquals("active",grants.grant(customer).get("token_status"));
    }

    @Test void databaseFailureRetriesSavingTheSameResponse() {
        bind();
        String customer=customerId();
        db.update("UPDATE speaker_grant SET access_expires_at_ms=0 WHERE customer_id=?",customer);
        var failOnce=new AtomicBoolean(true);
        var dbWithFailure=new JdbcTemplate(db.getDataSource()) {
            @Override public int update(String sql,Object... args) {
                if(sql.startsWith("UPDATE speaker_grant SET access_cipher=") && failOnce.getAndSet(false))
                    throw new TransientDataAccessResourceException("Fixture write failure");
                return super.update(sql,args);
            }
        };
        var worker=grantsUsing(dbWithFailure);
        var next=replacementTokens();
        when(market.refresh(anyString(),anyString())).thenReturn(next);
        worker.refreshDue();
        assertEquals("refreshing",grants.grant(customer).get("token_status"));
        worker.refreshDue();
        assertEquals(next.refresh(),grants.currentTokens(customer).refresh());
        verify(market,times(1)).refresh(anyString(),anyString());
    }

    @Test void requestedReauthorizationKeepsTheOriginalBinding() {
        bind();
        String customer=customerId();
        var previous=grants.grant(customer).get("generation").toString();
        long claim=System.currentTimeMillis()+60000;
        assertTrue(grants.claimCredentialCheck(customer,previous,System.currentTimeMillis(),claim));
        db.update("UPDATE speaker_grant SET token_status='uncertain',refresh_cipher=NULL WHERE customer_id=?",customer);
        var operations=new SpeakerOperations(db,devices,grants,mock(SpeakerVoice.class));
        var original=operations.state(identity);
        assertEquals("ready",original.get("use_status"));
        operations.requestReauthorization(customer,device,SpeakerSupport.number(original,"permission_version"),0);
        var requested=operations.state(identity);
        assertEquals("authorization_required",requested.get("use_status"));
        assertEquals("bound",requested.get("binding_state"));
        assertEquals("authorized",operations.useStatus(devices.load(identity,false)));
        login=logins.begin(identity).get("id").toString();
        when(market.poll(anyString())).thenReturn(new SpeakerMarket.Poll(replacementTokens(),null));
        advance();advance();
        var recovered=operations.state(identity);
        assertEquals("ready",recovered.get("use_status"));
        assertEquals(original.get("binding_version"),recovered.get("binding_version"));
        assertEquals(customer,customerId());
        assertEquals("active",grants.grant(customer).get("token_status"));
        assertNotEquals(previous,grants.grant(customer).get("generation"));
        assertEquals("pending",grants.grant(customer).get("credential_status"));
        assertFalse(grants.saveVerifiedCredential(customer,previous,claim,
                new SpeakerMarket.Credential("stale-key","https://fixture.example",null),true));
    }

    @Test void rechargeConsentForSameAccountPreservesDeviceBindingAndVoicePermission() {
        bind();
        String customer=customerId();
        var before=devices.load(identity,false);
        var replacement=replacementTokens();
        var consent=new SpeakerMarket.Tokens(replacement.subject(),replacement.access(),replacement.refresh(),replacement.expiresAt(),
                replacement.scope()+" "+SpeakerMarket.RECHARGE_SCOPE);
        when(market.beginRecharge()).thenReturn(new SpeakerMarket.DeviceAuthorization("payment-device-code","ABCD-EFGH",
                "https://fixture.example/device?user_code=ABCD-EFGH",600,5));
        login=logins.beginRecharge(identity).get("id").toString();
        when(market.poll(anyString())).thenReturn(new SpeakerMarket.Poll(consent,null));
        advance();advance();
        assertEquals("completed",logins.status(identity,login).get("status"));
        var after=devices.load(identity,false);
        assertEquals(customer,customerId());
        assertEquals(before.get("binding_version"),after.get("binding_version"));
        assertEquals(before.get("permission_version"),after.get("permission_version"));
        assertTrue(grants.currentTokens(customer).allowsRecharge());
    }

    private String customerId() {
        return db.queryForObject("SELECT customer_id FROM speaker_device_state WHERE device_id=?",String.class,device);
    }

    private SpeakerMarket.Tokens replacementTokens() {
        return new SpeakerMarket.Tokens(tokens.subject(),"new-access-fixture","new-refresh-fixture",
                System.currentTimeMillis()+300_000,SpeakerMarket.SCOPES);
    }

    private SpeakerGrants grantsUsing(JdbcTemplate template) {
        return new SpeakerGrants(template,settings,crypto,market,new DataSourceTransactionManager(db.getDataSource()));
    }
    private void bind() {
        authorize();when(market.credential(any())).thenReturn(new SpeakerMarket.Credential("key-fixture","https://fixture.example",null));advance();
    }
    @Test void signOutAndUnbindAreIdempotentAndKeepOtherDevicesAndApplicationKey() {
        bind();var operations=new SpeakerOperations(db,devices,grants,mock(SpeakerVoice.class));
        String customer=db.queryForObject("SELECT customer_id FROM speaker_device_state WHERE device_id=?",String.class,device);
        String generation=grants.grant(customer).get("generation").toString();
        long claim=System.currentTimeMillis()+60000;
        assertTrue(grants.claimCredentialCheck(customer,generation,System.currentTimeMillis(),claim));
        assertTrue(grants.saveVerifiedCredential(customer,generation,claim,
                new SpeakerMarket.Credential("shared-key","https://fixture.example",null),false));
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
            assertEquals(generation,grants.grant(customer).get("generation"));
            assertEquals("shared-key",crypto.decrypt(grants.grant(customer).get("credential_cipher").toString()));
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
        verify(market,never()).credential(any());
        assertEquals(0,db.queryForObject("SELECT COUNT(*) FROM speaker_customer WHERE issuer=? AND subject=?",Integer.class,settings.issuer(),tokens.subject()));
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
        assertEquals(tokens.subject(),row.get("subject"));
        assertEquals(1,((Number)row.get("device_count")).intValue());assertEquals(1,((Number)row.get("online_count")).intValue());
    }

    @Test void accountBalanceUsesTheBoundIdentityAndCachesNumbersAndFailures() {
        var voice = mock(SpeakerVoice.class);
        when(voice.ticket(any())).thenThrow(new SpeakerFailure(503, "DEFAULT_AGENT_REQUIRED"));
        var operations = new SpeakerOperations(db, devices, grants, voice);
        var controller = new SpeakerDeviceController(devices, logins, operations, null);
        assertEquals(401, assertThrows(SpeakerFailure.class, () -> controller.accountBalance(null, device)).status);
        assertEquals(403, assertThrows(SpeakerFailure.class, () -> operations.accountBalance(identity)).status);
        verify(market, never()).balance(any());
        bind();

        assertEquals(true, operations.state(identity).get("account_balance_allowed"));
        when(market.balance(any())).thenReturn(new BigDecimal("0.00000000"));
        assertEquals("0", operations.accountBalance(identity).balance());
        assertEquals("0", operations.accountBalance(identity).balance());
        verify(market).balance(argThat(grant -> tokens.subject().equals(grant.subject())));

        String customer = db.queryForObject("SELECT customer_id FROM speaker_device_state WHERE device_id=?", String.class, device);
        db.update("UPDATE speaker_customer SET balance_checked_at_ms=? WHERE id=?", 1L, customer);
        when(market.balance(any())).thenReturn(new BigDecimal("1234567890123456789012.34567890"));
        assertEquals("1234567890123456789012.3456789", operations.accountBalance(identity).balance());
        assertEquals("1234567890123456789012.3456789", operations.accountBalance(identity).balance());

        db.update("UPDATE speaker_customer SET balance_checked_at_ms=? WHERE id=?", 1L, customer);
        when(market.balance(any())).thenThrow(new SpeakerFailure(503, "MARKET_UNAVAILABLE"));
        long permission = operations.accountBalance(identity).permission_version();
        assertNull(operations.accountBalance(identity).balance());
        verify(market, times(3)).balance(any());
        operations.action(identity, "unbind", permission, "balance-test-unbind", "device");
        assertEquals(false, operations.state(identity).get("account_balance_allowed"));
        assertEquals(403, assertThrows(SpeakerFailure.class, () -> operations.accountBalance(identity)).status);
    }

    @Test void aBalanceResponseCannotFollowADeviceUnbind() {
        bind();
        var operations = new SpeakerOperations(db, devices, grants, mock(SpeakerVoice.class));
        long permission = SpeakerSupport.number(devices.load(identity, false), "permission_version");
        when(market.balance(any())).thenAnswer(grant -> {
            operations.action(identity, "unbind", permission, "balance-pending-unbind", "device");
            return BigDecimal.TEN;
        });
        assertEquals(409, assertThrows(SpeakerFailure.class, () -> operations.accountBalance(identity)).status);
    }
}
