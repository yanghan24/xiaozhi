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
    private SpeakerAgentConfiguration fullConfiguration;
    private SpeakerDevices.Identity identity;
    private String customer;
    private SpeakerMcp mcp;
    private SpeakerConversations conversations;
    @BeforeEach void setup() throws Exception {
        String url=System.getenv("SPRING_DATASOURCE_DRUID_URL");
        if(url==null || !url.startsWith("jdbc:mysql://172.16.18.104:13306/xiaozhi_dev?")) throw new IllegalStateException("Approved development database required");
        var source=new DriverManagerDataSource(url,"xiaozhi_dev",System.getenv("SPRING_DATASOURCE_DRUID_PASSWORD"));
        db=new JdbcTemplate(source);manager=new DataSourceTransactionManager(source);transaction=manager.getTransaction(new DefaultTransactionDefinition());
        db.queryForMap("SELECT * FROM speaker_runtime WHERE id=1 FOR UPDATE");
        db.update("UPDATE speaker_runtime SET default_agent_id=NULL,configured_agent_id=NULL WHERE id=1");
        Path key=directory.resolve("key");Files.writeString(key,Base64.getEncoder().encodeToString(new byte[32]));Files.setPosixFilePermissions(key,PosixFilePermissions.fromString("rw-------"));
        var settings=new SpeakerSettings(true,"https://fixture.example/oidc","fixture-client",key.toString(),"fixture-service-key-12345678901234567890");
        fullConfiguration=org.mockito.Mockito.mock(SpeakerAgentConfiguration.class);
        var crypto=new SpeakerCrypto(settings);devices=new SpeakerDevices(db,settings,manager);agents=new SpeakerAgents(db,settings,manager,fullConfiguration);
        grants=new SpeakerGrants(db,settings,crypto,org.mockito.Mockito.mock(SpeakerMarket.class),manager);var endpoints=org.mockito.Mockito.mock(xiaozhi.modules.sys.service.VoiceServiceEndpoints.class);
        org.mockito.Mockito.when(endpoints.deviceUrl()).thenReturn("ws://127.0.0.1:8000/xiaozhi/v1/");
        voice=new SpeakerVoice(db,settings,crypto,grants,agents,endpoints);
        mcp=new SpeakerMcp(db,crypto);conversations=new SpeakerConversations(db);
        org.springframework.test.util.ReflectionTestUtils.setField(voice,"conversations",conversations);
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
        String id=UUID.randomUUID().toString().replace("-","");
        db.update("INSERT INTO ai_agent(id,agent_name,system_prompt) VALUES (?,?,?)",id,name,"你是"+name);
        org.mockito.Mockito.when(fullConfiguration.resolve(org.mockito.ArgumentMatchers.eq(id),org.mockito.ArgumentMatchers.anyLong()))
                .thenReturn(Map.of("id",id,"prompt","你是"+name,"signature",id));
        return id;
    }
    private void select(String id) {
        agents.selectConfiguration(id,db.queryForObject("SELECT revision FROM speaker_runtime WHERE id=1",Long.class));
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
        var first=(Map<?,?>)voice.action("check",body);
        select(role("角色B","model-b"));
        assertEquals(true,((Map<?,?>)voice.action("check",body)).get("allowed"));
        var next=(Map<?,?>)voice.action("check",body);
        assertNotEquals(first.get("configuration_revision"),next.get("configuration_revision"));
        assertFalse(next.containsKey("api_key"));assertEquals(customer,devices.load(identity,false).get("customer_id"));
        assertThrows(SpeakerFailure.class,()->voice.action("model-access",body));
    }
    @Test void unbindRejectsOldTicketAndVoiceSessionWithoutRevokingApplicationKey() {
        select(role("测试助手","model-a"));var ticket=voice.ticket(devices.load(identity,false));
        var opened=(Map<?,?>)voice.action("open",Map.of("device_id",identity.id(),"token",ticket.get("token").toString()));
        new SpeakerOperations(db,devices,grants,voice).action(identity,"unbind",1,"fixture-unbind","device");
        assertThrows(SpeakerFailure.class,()->voice.action("check",Map.of("session",opened.get("session").toString())));
        assertThrows(SpeakerFailure.class,()->voice.action("open",Map.of("device_id",identity.id(),"token",ticket.get("token").toString())));
        assertTrue(SpeakerGrants.usable(grants.grant(customer),System.currentTimeMillis()));
    }
    @Test void selectsTheOriginalFullAgentWithoutCopyingItsConfiguration() {
        String id=UUID.randomUUID().toString().replace("-","");
        db.update("INSERT INTO ai_agent(id,agent_name,system_prompt) VALUES (?,?,?)",id,"完整配置夹具","完整提示词");
        long revision=db.queryForObject("SELECT revision FROM speaker_runtime WHERE id=1",Long.class);
        org.mockito.Mockito.when(fullConfiguration.resolve(org.mockito.ArgumentMatchers.eq(id),org.mockito.ArgumentMatchers.anyLong()))
                .thenReturn(Map.of("id",id,"prompt","完整提示词","signature","full:1"));
        agents.selectConfiguration(id,revision);
        assertEquals(id,agents.defaultConfiguration().get("agent_id"));
        assertEquals(0,db.queryForObject("SELECT COUNT(*) FROM speaker_agent WHERE id=?",Integer.class,id));
        assertThrows(SpeakerFailure.class,()->agents.selectConfiguration(id,revision));
    }
    @Test void connectionIdentityNeedsNoResourceKeyAndRejectsRevokedIdentity() {
        select(role("原版提供者","any-provider"));
        db.update("UPDATE speaker_grant SET credential_cipher=NULL,credential_status='not_requested' WHERE customer_id=?",customer);
        var ticket=voice.ticket(devices.load(identity,false));
        var opened=(Map<?,?>)voice.action("open",Map.of("device_id",identity.id(),"token",ticket.get("token").toString()));
        var session=Map.of("session",opened.get("session").toString());
        assertEquals(true,((Map<?,?>)voice.action("check",session)).get("allowed"));
        db.update("UPDATE speaker_grant SET token_status='reauth_required' WHERE customer_id=?",customer);
        assertThrows(SpeakerFailure.class,()->voice.action("check",session));
    }
    @Test void mcpCredentialsAreEncryptedAndOnlySelectedEnabledServicesReachOriginalRuntime() {
        String role=role("工具角色","original");select(role);
        String id=((Map<?,?>)mcp.save(null,new SpeakerMcp.Configuration("协议夹具","https://fixture.example/mcp","streamable-http",
                Map.of("Authorization","Bearer secret-fixture"),false,false,0))).get("id").toString();
        mcp.select(role,List.of(id));
        assertTrue(mcp.configuration(role).isEmpty());
        assertFalse(mcp.list().toString().contains("secret-fixture"));
        assertFalse(db.queryForObject("SELECT headers_cipher FROM speaker_mcp WHERE id=?",String.class,id).contains("secret-fixture"));
        String previous=agents.configurationRevision();
        mcp.save(id,new SpeakerMcp.Configuration("协议夹具","https://fixture.example/mcp","streamable-http",null,false,true,1));
        assertNotEquals(previous,agents.configurationRevision());
        assertEquals(Map.of(id, Map.of(
                "url", "https://fixture.example/mcp",
                "transport", "streamable-http",
                "headers", Map.of("Authorization", "Bearer secret-fixture"))), mcp.configuration(role));
        assertTrue(mcp.configuration(role("未关联角色","original")).isEmpty());
        assertThrows(SpeakerFailure.class,()->mcp.save(id,new SpeakerMcp.Configuration("过期编辑","https://fixture.example/mcp","sse",null,false,true,1)));
    }
    @Test void conversationPinsOwnerAndRoleAndMemoryDoesNotCrossConfigurationScopes() {
        String firstRole=role("角色A","original");select(firstRole);
        var ticket=voice.ticket(devices.load(identity,false));
        var opened=(Map<?,?>)voice.action("open",Map.of("device_id",identity.id(),"token",ticket.get("token").toString()));
        String session=opened.get("session").toString();
        var receipt=(Map<?,?>)voice.action("conversation",Map.of("session",session,"configuration_revision",opened.get("configuration_revision").toString()));
        var old=conversations.find(receipt.get("history_id").toString());
        assertEquals(customer,old.get("customer_id"));assertEquals(firstRole,old.get("agent_id"));
        conversations.saveMemory(old.get("memory_scope").toString(),"属于原用户和角色的记忆");
        select(role("角色B","original"));
        var current=(Map<?,?>)voice.action("check",Map.of("session",session));
        assertEquals("",conversations.memory(current.get("memory_scope").toString()));
        assertEquals("属于原用户和角色的记忆",conversations.memory(old.get("memory_scope").toString()));
        assertEquals(firstRole,conversations.find(receipt.get("history_id").toString()).get("agent_id"));
    }

    @Test
    void conversationDisplayUsesHistoricalOwnerAfterDeviceRebinding() {
        String agentId = role("共享智能体", "original");
        var firstDevice = devices.load(identity, false);
        String oldSession = conversations.create(firstDevice, agentId + ":1", "a".repeat(64));
        String oldSubject = db.queryForObject("SELECT subject FROM speaker_customer WHERE id=?", String.class, customer);
        db.update("UPDATE speaker_customer SET display_name=? WHERE id=?", "原用户", customer);

        String nextCustomer = UUID.randomUUID().toString();
        String nextSubject = "next-owner-" + UUID.randomUUID();
        db.update("INSERT INTO speaker_customer(id,issuer,subject,display_name,created_at_ms) VALUES (?,?,?,?,?)",
                nextCustomer, "https://fixture.example/oidc", nextSubject, "新用户", System.currentTimeMillis());
        db.update("UPDATE speaker_device_state SET customer_id=? WHERE device_id=?", nextCustomer, identity.id());
        String newSession = conversations.create(devices.load(identity, false), agentId + ":1", "b".repeat(64));

        var owners = conversations.findOwners(agentId, List.of(oldSession, newSession, UUID.randomUUID().toString()));
        assertEquals(Set.of(oldSession, newSession), owners.keySet());
        assertEquals("原用户", owners.get(oldSession).customerName());
        assertEquals(oldSubject, owners.get(oldSession).marketAccountId());
        assertEquals(firstDevice.get("name"), owners.get(oldSession).deviceName());
        assertEquals(identity.id(), owners.get(oldSession).deviceId());
        assertEquals(nextSubject, owners.get(newSession).marketAccountId());
        assertTrue(conversations.findOwners(role("另一智能体", "original"), List.of(oldSession)).isEmpty());
    }
}
