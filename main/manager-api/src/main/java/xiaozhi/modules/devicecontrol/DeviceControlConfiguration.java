package xiaozhi.modules.devicecontrol;

import java.time.Clock;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;

@Configuration
public class DeviceControlConfiguration {
    @Bean
    @ConditionalOnMissingBean(name = "deviceControlClock")
    public Clock deviceControlClock() {
        return Clock.systemUTC();
    }
}
