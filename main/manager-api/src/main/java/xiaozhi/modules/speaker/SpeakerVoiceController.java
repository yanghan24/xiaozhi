package xiaozhi.modules.speaker;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Map;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/internal/speaker-voice/v2")
@ConditionalOnProperty(name="speaker.enabled",havingValue="true")
public class SpeakerVoiceController {
    private final SpeakerVoice voice;
    private final SpeakerSettings settings;
    public SpeakerVoiceController(SpeakerVoice voice,SpeakerSettings settings) {this.voice=voice;this.settings=settings;}
    @PostMapping("/{action}")
    public Object action(@PathVariable String action,@RequestHeader(name="X-Voice-Service-Key",required=false) String key,
            @RequestBody Map<String,String> body) {
        if(settings.voiceServiceKey().length()<32 || key==null || !MessageDigest.isEqual(settings.voiceServiceKey().getBytes(StandardCharsets.UTF_8),key.getBytes(StandardCharsets.UTF_8)))
            throw new SpeakerFailure(401,"VOICE_DENIED");
        if(body.size()>2 || body.values().stream().anyMatch(v -> v==null || v.length()>2048)) throw new SpeakerFailure(400,"INVALID_REQUEST");
        return ResponseEntity.ok().header("Cache-Control","no-store").body(voice.action(action,body));
    }
}
