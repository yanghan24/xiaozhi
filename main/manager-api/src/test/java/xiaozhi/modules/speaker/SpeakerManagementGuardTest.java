package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import java.util.concurrent.atomic.AtomicBoolean;
import org.junit.jupiter.api.Test;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.mock.web.MockHttpServletResponse;

class SpeakerManagementGuardTest {
    @Test void productConsoleRejectsRegistrationAndLegacyCustomerPages() throws Exception {
        for(String path:new String[]{"/user/register","/user/retrieve-password","/customer/oidc/callback","/bind/old-link"}) {
            var request=new MockHttpServletRequest("POST","/xiaozhi"+path);request.setContextPath("/xiaozhi");
            var response=new MockHttpServletResponse();var called=new AtomicBoolean();
            new SpeakerManagementGuard().doFilter(request,response,(req,res)->called.set(true));
            assertEquals(403,response.getStatus());assertFalse(called.get());
        }
    }
    @Test void leavesHardwareAuthenticationToDeviceAuthorityAndDisablesCaching() throws Exception {
        var request=new MockHttpServletRequest("POST","/xiaozhi/speaker/v1/heartbeat");request.setContextPath("/xiaozhi");
        var response=new MockHttpServletResponse();var called=new AtomicBoolean();
        new SpeakerManagementGuard().doFilter(request,response,(req,res)->called.set(true));
        assertTrue(called.get());assertEquals("no-store",response.getHeader("Cache-Control"));
    }
}
