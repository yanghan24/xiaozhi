package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
import java.util.Date;
import org.apache.shiro.authc.DisabledAccountException;
import org.junit.jupiter.api.Test;
import org.springframework.test.util.ReflectionTestUtils;
import xiaozhi.modules.security.controller.LoginController;
import xiaozhi.modules.security.dto.LoginDTO;
import xiaozhi.modules.security.entity.SysUserTokenEntity;
import xiaozhi.modules.security.oauth2.Oauth2Realm;
import xiaozhi.modules.security.oauth2.Oauth2Token;
import xiaozhi.modules.security.service.ShiroService;
import xiaozhi.modules.sys.entity.SysUserEntity;

class SpeakerAdminAccessTest {
    @Test void registrationCannotBeReopenedByLegacyRegistrationSetting() {
        var controller=new LoginController(null,null,null,null,null);
        ReflectionTestUtils.setField(controller,"speakerAdminOnly",true);
        try(var messages=mockStatic(xiaozhi.common.utils.MessageUtils.class)) {
            assertThrows(xiaozhi.common.exception.RenException.class,()->controller.register(new LoginDTO()));
        }
    }
    @Test void existingOrdinaryLocalTokenCannotEnterProductConsole() {
        var service=mock(ShiroService.class);var realm=new Oauth2Realm();
        ReflectionTestUtils.setField(realm,"shiroService",service);
        ReflectionTestUtils.setField(realm,"speakerAdminOnly",true);
        var token=new SysUserTokenEntity();token.setUserId(7L);token.setExpireDate(new Date(System.currentTimeMillis()+60000));
        var ordinary=new SysUserEntity();ordinary.setSuperAdmin(0);ordinary.setStatus(1);
        when(service.getByToken("fixture-token")).thenReturn(token);when(service.getUser(7L)).thenReturn(ordinary);
        assertThrows(DisabledAccountException.class,()->realm.getAuthenticationInfo(new Oauth2Token("fixture-token")));
    }
}
