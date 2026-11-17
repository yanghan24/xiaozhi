package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.*;
import org.junit.jupiter.api.*;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.datasource.*;
import org.springframework.transaction.TransactionStatus;
import org.springframework.transaction.support.DefaultTransactionDefinition;

/** All fixtures and default-role changes are in one rolled-back transaction on the 104 development database. */
class SpeakerRoleDatabaseTest {
    @TempDir Path directory;
    private DataSourceTransactionManager manager;
    private TransactionStatus transaction;
    private JdbcTemplate db;
    private SpeakerDevices devices;
    private SpeakerAgents agents;
    private SpeakerVoice voice;
    private SpeakerGrants grants;
    private SpeakerDevices.Identity identity;
    private String customer;
    @BeforeEach void setup() throws Exception {
        String url=System.getenv("SPRING_DATASOURCE_DRUID_URL");
        if(url==null || !url.startsWith("jdbc:mysql://172.16.18.104:13306/xiaozhi_dev?")) throw new IllegalStateException("Approved development database required");
        var source=new DriverManagerDataSource(url,"xiaozhi_dev",System.getenv("SPRING_DATASOURCE_DRUID_PASSWORD"));
        db=new JdbcTemplate(source);manager=new DataSourceTransactionManager(source);transaction=manager.getTransaction(new DefaultTransactionDefinition());
        db.queryForMap("SELECT * FROM speaker_runtime WHERE id=1 FOR UPDATE");
        db.update("UPDATE speaker_runtime SET default_agent_id=NULL WHERE id=1");
        Path key=directory.resolve("key");Files.writeString(key,Base64.getEncoder().encodeToString(new byte[32]));Files.setPosixFilePermissions(key,PosixFilePermissions.fromString("rw-------"));
        var settings=new SpeakerSettings(true,"https://fixture.example/oidc","fixture-client",key.toString(),"ws://127.0.0.1:8000/xiaozhi/v1/","fixture-service-key-12345678901234567890");
        var crypto=new SpeakerCrypto(settings);devices=new SpeakerDevices(db,settings,manager);agents=new SpeakerAgents(db,settings,manager);
        grants=new SpeakerGrants(db,settings,crypto,org.mockito.Mockito.mock(SpeakerMarket.class),manager);voice=new SpeakerVoice(db,settings,crypto,grants,agents);
        String device="speaker-role-test-"+UUID.randomUUID();var registration=devices.register(device,"角色协议夹具","p4-test",0);
        identity=devices.authenticate("Bearer "+registration.get("credential"),device);
        customer=UUID.randomUUID().toString();
        db.update("INSERT INTO speaker_customer(id,issuer,subject,created_at_ms) VALUES (?,?,?,?)",customer,settings.issuer(),"role-owner-"+UUID.randomUUID(),System.currentTimeMillis());
        db.update("INSERT INTO speaker_grant(customer_id,client_id,access_cipher,access_expires_at_ms,token_updated_at_ms,credential_cipher,credential_status,credential_base,generation) VALUES (?,?,?,?,?,?,'active',?,?)",
                customer,settings.clientId(),crypto.encrypt("access-fixture"),System.currentTimeMillis()+300000,System.currentTimeMillis(),crypto.encrypt("key-fixture"),"https://fixture.example",SpeakerSupport.token());
        db.update("UPDATE speaker_device_state SET customer_id=?,use_enabled=TRUE WHERE device_id=?",customer,device);
    }
    @AfterEach void rollback() { if(transaction!=null) manager.rollback(transaction); }
    private String role(String name,String model) {
        return ((Map<?,?>)agents.save(null,new SpeakerAgents.Configuration(name,"你是"+name,model,"rtasr-llm","zh-CN-XiaoxiaoNeural",0))).get("id").toString();
    }
    private void select(String id) {
        agents.select(id,db.queryForObject("SELECT revision FROM speaker_runtime WHERE id=1",Long.class));
    }
    @Test void defaultMustBeExplicitlySetBeforeVoiceUse() {
        assertEquals("DEFAULT_AGENT_REQUIRED",assertThrows(SpeakerFailure.class,()->voice.ticket(devices.load(identity,false))).getMessage());
        String id=role("测试助手","model-a");
        assertThrows(SpeakerFailure.class,()->voice.ticket(devices.load(identity,false)));
        select(id);assertNotNull(voice.ticket(devices.load(identity,false)).get("token"));
    }
    @Test void changingDefaultDoesNotRevokeSessionOrBindingAndNextSnapshotUsesNewRole() {
        select(role("角色A","model-a"));var ticket=voice.ticket(devices.load(identity,false));
        var opened=(Map<?,?>)voice.action("open",Map.of("device_id",identity.id(),"token",ticket.get("token").toString()));
        var body=Map.of("session",opened.get("session").toString());
        var first=(Map<?,?>)voice.action("model-access",body);assertEquals("model-a",first.get("model"));
        select(role("角色B","model-b"));
        assertEquals(true,((Map<?,?>)voice.action("check",body)).get("allowed"));
        var next=(Map<?,?>)voice.action("model-access",body);assertEquals("model-b",next.get("model"));
        assertEquals("key-fixture",next.get("api_key"));assertEquals(customer,devices.load(identity,false).get("customer_id"));
    }
    @Test void unbindRejectsOldTicketAndVoiceSessionWithoutRevokingApplicationKey() {
        select(role("测试助手","model-a"));var ticket=voice.ticket(devices.load(identity,false));
        var opened=(Map<?,?>)voice.action("open",Map.of("device_id",identity.id(),"token",ticket.get("token").toString()));
        new SpeakerOperations(db,devices,grants,voice).action(identity,"unbind",1,"fixture-unbind","device");
        assertThrows(SpeakerFailure.class,()->voice.action("check",Map.of("session",opened.get("session").toString())));
        assertThrows(SpeakerFailure.class,()->voice.action("open",Map.of("device_id",identity.id(),"token",ticket.get("token").toString())));
        assertTrue(SpeakerGrants.usable(grants.grant(customer),System.currentTimeMillis()));
    }
    @Test void rejectsIncompleteAndStaleRoleUpdates() {
        assertThrows(SpeakerFailure.class,()->agents.save(null,new SpeakerAgents.Configuration("角色","","model-a","rtasr-llm","voice",0)));
        String id=role("角色","model-a");
        assertEquals("AGENT_CHANGED",assertThrows(SpeakerFailure.class,()->agents.save(id,new SpeakerAgents.Configuration("角色","提示词","model-b","rtasr-llm","voice",0))).getMessage());
    }
}
