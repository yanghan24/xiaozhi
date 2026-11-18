package xiaozhi.modules.config.init;

import java.nio.file.Files;
import java.nio.file.Path;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Component;

import xiaozhi.common.constant.Constant;
import xiaozhi.modules.sys.dto.SysUserDTO;
import xiaozhi.modules.sys.service.SysParamsService;
import xiaozhi.modules.sys.service.SysUserService;

@Component
@ConditionalOnProperty(name = "xiaozhi.production.enabled", havingValue = "true")
public class ProductionBootstrap implements ApplicationRunner {
    private final JdbcTemplate db;
    private final SysUserService users;
    private final SysParamsService parameters;
    private final String passwordFile;
    private final String websocket;
    private final String ota;

    public ProductionBootstrap(JdbcTemplate db, SysUserService users, SysParamsService parameters,
            @Value("${xiaozhi.production.bootstrap-password-file:}") String passwordFile,
            @Value("${xiaozhi.production.websocket:}") String websocket,
            @Value("${xiaozhi.production.ota:}") String ota) {
        this.db = db;
        this.users = users;
        this.parameters = parameters;
        this.passwordFile = passwordFile;
        this.websocket = websocket;
        this.ota = ota;
    }

    @Override
    public void run(ApplicationArguments args) throws Exception {
        if (db.queryForObject("SELECT COUNT(*) FROM sys_user", Long.class) != 0) {
            return;
        }
        if (passwordFile.isBlank() || websocket.isBlank() || ota.isBlank()) {
            throw new IllegalStateException("Empty production database requires initial deployment settings");
        }
        // Seed only a fresh database, using the same password and first-admin
        // handling as the normal user service. No HTTP bootstrap route.
        parameters.updateValueByCode(Constant.SERVER_WEBSOCKET, websocket);
        parameters.updateValueByCode(Constant.SERVER_OTA, ota);
        SysUserDTO admin = new SysUserDTO();
        admin.setUsername("admin");
        admin.setPassword(Files.readString(Path.of(passwordFile)).strip());
        users.save(admin);
    }
}
