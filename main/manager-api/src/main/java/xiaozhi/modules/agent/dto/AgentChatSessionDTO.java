package xiaozhi.modules.agent.dto;

import java.time.LocalDateTime;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Data;

/**
 * 智能体会话列表DTO
 */
@Data
public class AgentChatSessionDTO {
    /**
     * 会话ID
     */
    private String sessionId;

    /**
     * 会话时间
     */
    private LocalDateTime createdAt;

    /**
     * 聊天条数
     */
    private Integer chatCount;

    /**
     * 会话标题
     */
    private String title;

    @JsonInclude(JsonInclude.Include.NON_NULL)
    private SpeakerOwner speakerOwner;

    public record SpeakerOwner(String customerName, String marketAccountId, String deviceName, String deviceId) {
    }
}
