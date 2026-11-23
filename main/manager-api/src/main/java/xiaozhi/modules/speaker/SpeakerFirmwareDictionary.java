package xiaozhi.modules.speaker;

import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.stereotype.Component;
import xiaozhi.common.redis.RedisKeys;
import xiaozhi.common.redis.RedisUtils;

/** Make the additive board dictionary migration visible after startup. */
@Component
@ConditionalOnProperty(name="speaker.enabled",havingValue="true")
public class SpeakerFirmwareDictionary implements ApplicationRunner {
    private final RedisUtils cache;
    public SpeakerFirmwareDictionary(RedisUtils cache) { this.cache=cache; }
    @Override public void run(ApplicationArguments args) { cache.delete(RedisKeys.getDictDataByTypeKey("FIRMWARE_TYPE")); }
}
