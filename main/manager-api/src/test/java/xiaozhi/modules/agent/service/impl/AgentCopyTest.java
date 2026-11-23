package xiaozhi.modules.agent.service.impl;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import java.util.List;
import org.junit.jupiter.api.*;
import org.mockito.MockedStatic;
import org.springframework.test.util.ReflectionTestUtils;
import xiaozhi.common.exception.RenException;
import xiaozhi.common.user.UserDetail;
import xiaozhi.modules.agent.dao.*;
import xiaozhi.modules.agent.entity.*;
import xiaozhi.modules.agent.service.*;
import xiaozhi.modules.correctword.service.CorrectWordFileService;
import xiaozhi.modules.device.service.DeviceService;
import xiaozhi.modules.security.user.SecurityUser;
import xiaozhi.modules.speaker.SpeakerMcp;

class AgentCopyTest {
    AgentDao dao; AgentTagDao tagDao; AgentServiceImpl service;
    AgentPluginMappingService plugins; AgentContextProviderService contexts;
    CorrectWordFileService words; AgentTagService tags; AgentSnapshotService snapshots;
    DeviceService devices; AgentChatHistoryService history; SpeakerMcp mcp;
    MockedStatic<SecurityUser> identity; MockedStatic<xiaozhi.common.utils.MessageUtils> messages; UserDetail administrator; AgentEntity source;
    AgentEntity[] inserted;
    @BeforeEach void setup() {
        dao=mock(AgentDao.class);tagDao=mock(AgentTagDao.class);plugins=mock(AgentPluginMappingService.class);
        contexts=mock(AgentContextProviderService.class);words=mock(CorrectWordFileService.class);
        tags=mock(AgentTagService.class);snapshots=mock(AgentSnapshotService.class);
        devices=mock(DeviceService.class);history=mock(AgentChatHistoryService.class);mcp=mock(SpeakerMcp.class);
        service=new AgentServiceImpl(dao,tagDao,null,null,null,devices,plugins,history,null,null,contexts,tags,words,snapshots);
        ReflectionTestUtils.setField(service,"baseDao",dao);ReflectionTestUtils.setField(service,"speakerMcp",mcp);
        administrator=new UserDetail();administrator.setId(202L);administrator.setSuperAdmin(1);
        messages=mockStatic(xiaozhi.common.utils.MessageUtils.class);
        identity=mockStatic(SecurityUser.class);identity.when(SecurityUser::getUser).thenReturn(administrator);
        identity.when(SecurityUser::getUserId).thenReturn(202L);
        source=new AgentEntity();source.setId("original");source.setUserId(101L);source.setCreator(101L);
        source.setAgentName("原角色");source.setSystemPrompt("保留的角色规则");source.setSummaryMemory("原用户的秘密记忆");
        source.setAgentCode("old-code");source.setSort(19);source.setLlmModelId("model");source.setTtsRate(0);
        source.setTtsVolume(0);source.setChatHistoryConf(2);
        when(dao.selectByIdForUpdate("original")).thenReturn(source);
        when(plugins.list(any(com.baomidou.mybatisplus.core.conditions.Wrapper.class))).thenReturn(List.of());when(tagDao.selectByAgentId("original")).thenReturn(List.of());
        when(words.getAgentCorrectWordFileIds("original")).thenReturn(List.of());
        inserted=new AgentEntity[1];when(dao.insert(any(AgentEntity.class))).thenAnswer(call->{
            inserted[0]=call.getArgument(0);inserted[0].setId("new-copy");return 1;
        });
    }
    @AfterEach void close() { identity.close(); messages.close(); }
    @Test void copiesConfigurationButNeverSourceIdentityHistoryOrMemory() {
        AgentPluginMapping plugin=new AgentPluginMapping();plugin.setId(99L);plugin.setAgentId("original");
        plugin.setPluginId("weather");plugin.setParamInfo("{\"city\":\"长沙\"}");when(plugins.list(any(com.baomidou.mybatisplus.core.conditions.Wrapper.class))).thenReturn(List.of(plugin));
        AgentContextProviderEntity context=new AgentContextProviderEntity();context.setId("old-context");
        context.setAgentId("original");context.setContextProviders(List.of());when(contexts.getByAgentId("original")).thenReturn(context);
        AgentTagEntity tag=new AgentTagEntity();tag.setId("tag");when(tagDao.selectByAgentId("original")).thenReturn(List.of(tag));
        when(words.getAgentCorrectWordFileIds("original")).thenReturn(List.of("word-file"));
        assertEquals("new-copy",service.copyAgent("original"));AgentEntity copy=inserted[0];
        assertEquals("原角色（副本）",copy.getAgentName());assertEquals(202L,copy.getUserId());assertEquals(202L,copy.getCreator());
        assertEquals("",copy.getSummaryMemory());assertEquals("保留的角色规则",copy.getSystemPrompt());
        assertEquals("model",copy.getLlmModelId());assertEquals(0,copy.getTtsRate());assertEquals(0,copy.getTtsVolume());
        assertNotEquals(source.getAgentCode(),copy.getAgentCode());assertEquals(0,copy.getSort());assertNull(copy.getUpdater());
        verify(plugins).saveBatch(argThat(list->list.size()==1 && list.iterator().next().getId()==null
                && "new-copy".equals(list.iterator().next().getAgentId()) && plugin.getParamInfo().equals(list.iterator().next().getParamInfo())),anyInt());
        verify(contexts).saveOrUpdateByAgentId(argThat(v->v.getId()==null && "new-copy".equals(v.getAgentId())));
        verify(words).saveAgentCorrectWords("new-copy",List.of("word-file"));verify(tags).saveAgentTags("new-copy",List.of("tag"),null);
        verify(mcp).copyAssociations("original","new-copy");verify(snapshots).createSnapshot("new-copy","initial");
        verifyNoInteractions(devices,history);assertEquals("原用户的秘密记忆",source.getSummaryMemory());
        assertEquals("original",plugin.getAgentId());assertEquals(99L,plugin.getId());assertEquals("old-context",context.getId());
    }
    @Test void refusesAnotherUsersRoleBeforeCreatingAnyCopy() {
        administrator.setSuperAdmin(0);assertThrows(RenException.class,()->service.copyAgent("original"));
        verify(dao,never()).insert(any(AgentEntity.class));verifyNoInteractions(plugins,contexts,words,tags,snapshots,mcp);
    }
    @Test void missingSourceCreatesNothing() {
        assertThrows(RenException.class,()->service.copyAgent("missing"));verify(dao,never()).insert(any(AgentEntity.class));
    }
    @Test void blankDraftCanBeCopiedAndLongNamesFitWithoutBreakingUnicode() {
        source.setAgentName("😀".repeat(64));source.setSystemPrompt("");source.setLlmModelId(null);
        service.copyAgent("original");assertEquals(64,inserted[0].getAgentName().codePointCount(0,inserted[0].getAgentName().length()));
        assertNull(inserted[0].getLlmModelId());assertEquals("",inserted[0].getSystemPrompt());
    }
}
