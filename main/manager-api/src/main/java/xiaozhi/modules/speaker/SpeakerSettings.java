package xiaozhi.modules.speaker;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

/** Deployment inputs only. Identity-provider endpoints are discovered, never supplied by a device. */
@Component
public record SpeakerSettings(
        @Value("${speaker.enabled:false}") boolean enabled,
        @Value("${speaker.issuer:https://aimarket.hncig.cn/oidc}") String issuer,
        @Value("${speaker.client-id:}") String clientId,
        @Value("${speaker.encryption-key-file:}") String encryptionKeyFile,
        @Value("${speaker.voice-url:}") String voiceUrl,
        @Value("${speaker.voice-service-key:}") String voiceServiceKey) {
    public void requireEnabled() {
        if (!enabled) throw new SpeakerFailure(503, "SPEAKER_DISABLED");
    }
    @Override public String toString() { return "SpeakerSettings[redacted]"; }
}
