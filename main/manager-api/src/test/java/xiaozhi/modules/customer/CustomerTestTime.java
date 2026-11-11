package xiaozhi.modules.customer;

import java.time.*;
import java.util.concurrent.atomic.AtomicLong;
import org.springframework.boot.test.context.TestConfiguration;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Primary;

@TestConfiguration
class CustomerTestTime {
    static final AtomicLong offset = new AtomicLong();
    static final AtomicLong frozen = new AtomicLong();
    @Bean @Primary @org.springframework.beans.factory.annotation.Qualifier("customerTime")
    Clock testCustomerClock() {
        return new Clock() {
            public ZoneId getZone() { return ZoneOffset.UTC; }
            public Clock withZone(ZoneId zone) { return this; }
            public Instant instant() { return Instant.ofEpochMilli((frozen.get()==0 ? System.currentTimeMillis() : frozen.get()) + offset.get()); }
        };
    }
}
