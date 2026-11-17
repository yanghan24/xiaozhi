package xiaozhi.modules.config.controller;

import java.util.List;

import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import jakarta.validation.Valid;
import lombok.AllArgsConstructor;
import xiaozhi.common.utils.Result;
import xiaozhi.common.validator.ValidatorUtils;
import xiaozhi.modules.config.dto.AgentModelsDTO;
import xiaozhi.modules.config.dto.CorrectWordsDTO;
import xiaozhi.modules.config.service.ConfigService;

/**
 * xiaozhi-server 配置获取
 *
 * @since 1.0.0
 */
@RestController
@RequestMapping("config")
@Tag(name = "参数管理")
@lombok.RequiredArgsConstructor
public class ConfigController {
    private final ConfigService configService;
    @jakarta.annotation.Resource private xiaozhi.modules.model.market.MarketModelRuntime marketModels;
    @jakarta.annotation.Resource private xiaozhi.modules.speaker.SpeakerSettings speakerSettings;
    @jakarta.annotation.Resource private xiaozhi.modules.speaker.SpeakerVoice speakerVoice;
    @jakarta.annotation.Resource private xiaozhi.modules.speaker.SpeakerAgents speakerAgents;
    @jakarta.annotation.Resource private xiaozhi.modules.speaker.SpeakerMcp speakerMcp;
    @jakarta.annotation.Resource private xiaozhi.modules.speaker.SpeakerConversations speakerConversations;

    @PostMapping("server-base")
    @Operation(summary = "服务端获取配置接口")
    public Result<Object> getConfig() {
        java.util.Map<String,Object> config = new java.util.HashMap<>(configService.getConfig(true));
        config.put("speaker_identity_enabled",speakerSettings.enabled());
        return new Result<Object>().ok(config);
    }

    @PostMapping("agent-models")
    @Operation(summary = "获取智能体模型")
    public Result<Object> getAgentModels(@Valid @RequestBody AgentModelsDTO dto) {
        // 效验数据
        ValidatorUtils.validateEntity(dto);
        Object models;
        if(speakerSettings.enabled()) {
            speakerVoice.requireDevice(dto.getMacAddress());
            String revision=speakerAgents.configurationRevision();
            var configured=configService.getAgentConfiguration(speakerAgents.configurationId(),dto.getSelectedModule());
            configured.put("mcp_servers",speakerMcp.configuration(revision.split(":",2)[0]));
            if(marketModels.hasMarket(configured)) {
                String customer=speakerVoice.customerForSession(dto.getMacAddress(),dto.getVoiceSession());
                configured=marketModels.configure(configured,customer);
                if(!customer.equals(speakerVoice.customerForSession(dto.getMacAddress(),dto.getVoiceSession())))
                    throw new xiaozhi.modules.speaker.SpeakerFailure(403,"VOICE_DENIED");
            }
            if(!revision.equals(speakerAgents.configurationRevision()))
                throw new xiaozhi.modules.speaker.SpeakerFailure(409,"AGENT_CHANGED");
            configured.put("configuration_revision",revision);
            configured.put("memory_scope",speakerVoice.memoryScope(dto.getMacAddress(),revision));
            // A shared role's legacy summary is not a customer's personal memory.
            configured.put("summaryMemory", speakerConversations.memory(speakerVoice.memoryScope(dto.getMacAddress(),revision)));
            models=configured;
        } else models = configService.getAgentModels(dto.getMacAddress(), dto.getSelectedModule());
        return new Result<Object>().ok(models);
    }

    @PostMapping("correct-words")
    @Operation(summary = "获取智能体替换词")
    public Result<Object> getCorrectWords(@Valid @RequestBody CorrectWordsDTO dto) {
        ValidatorUtils.validateEntity(dto);
        List<String> list = speakerSettings.enabled() ? configService.getAgentCorrectWords(speakerAgents.configurationId()) : configService.getCorrectWords(dto.getMacAddress());
        return new Result<Object>().ok(list);
    }

    @PostMapping("device-session/{action}")
    public Result<Object> deviceSession(@org.springframework.web.bind.annotation.PathVariable String action,
            @RequestBody java.util.Map<String,String> body) {
        speakerSettings.requireEnabled();
        if(body.size()>2 || body.values().stream().anyMatch(v->v==null || v.length()>2048))
            throw new xiaozhi.modules.speaker.SpeakerFailure(400,"INVALID_REQUEST");
        return new Result<Object>().ok(speakerVoice.action(action,body));
    }

    @org.springframework.web.bind.annotation.ExceptionHandler(xiaozhi.modules.speaker.SpeakerFailure.class)
    public org.springframework.http.ResponseEntity<?> identityFailure(xiaozhi.modules.speaker.SpeakerFailure failure) {
        return org.springframework.http.ResponseEntity.status(failure.status).body(
                new Result<>().error(failure.status, failure.getMessage()));
    }
}
