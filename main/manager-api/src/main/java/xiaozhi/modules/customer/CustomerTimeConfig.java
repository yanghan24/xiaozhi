package xiaozhi.modules.customer;

import java.time.Clock;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
class CustomerTimeConfig {
    @Bean @org.springframework.beans.factory.annotation.Qualifier("customerTime") Clock customerClock() { return Clock.systemUTC(); }
}
