package xiaozhi.modules.devicecontrol;

import java.time.Clock;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.boot.web.servlet.FilterRegistrationBean;
import org.springframework.core.Ordered;

@Configuration
public class DeviceControlConfiguration {
    @Bean
    public FilterRegistrationBean<DeviceDeliveryMediaFilter> deviceDeliveryMediaType() {
        var registration = new FilterRegistrationBean<>(new DeviceDeliveryMediaFilter());
        registration.addUrlPatterns("/device-delivery/*");
        registration.setOrder(Ordered.HIGHEST_PRECEDENCE + 9);
        return registration;
    }

    @Bean
    public FilterRegistrationBean<DeviceControlBodyFilter> deviceControlBodyLimit() {
        var registration = new FilterRegistrationBean<>(new DeviceControlBodyFilter());
        registration.addUrlPatterns("/device-control/*");
        registration.addUrlPatterns("/admin/product-ota/*");
        registration.addUrlPatterns("/device-delivery/*");
        registration.setOrder(Ordered.HIGHEST_PRECEDENCE + 10);
        return registration;
    }

    @Bean
    @ConditionalOnMissingBean(name = "deviceControlClock")
    public Clock deviceControlClock() {
        return Clock.systemUTC();
    }
}
