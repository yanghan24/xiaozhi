package xiaozhi.modules.speaker;

import static org.junit.jupiter.api.Assertions.*;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Base64;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

class SpeakerCryptoTest {
    @TempDir Path directory;
    private SpeakerCrypto cipher(String client) throws Exception {
        Path key=directory.resolve("identity.key");
        Files.writeString(key,Base64.getEncoder().encodeToString(new byte[32]));
        Files.setPosixFilePermissions(key,PosixFilePermissions.fromString("rw-------"));
        return new SpeakerCrypto(new SpeakerSettings(true,"https://aimarket.hncig.cn/oidc",client,key.toString(),"",""));
    }
    @Test void encryptsWithFreshIvAndAuthenticatesApplication() throws Exception {
        var crypto=cipher("app-one");
        String one=crypto.encrypt("token-fixture"),two=crypto.encrypt("token-fixture");
        assertNotEquals(one,two);assertFalse(one.contains("token-fixture"));
        assertEquals("token-fixture",crypto.decrypt(one));
        var other=cipher("app-two");
        assertThrows(SpeakerFailure.class,()->other.decrypt(one));
        assertThrows(SpeakerFailure.class,()->crypto.decrypt(one.substring(1)));
    }
    @Test void rejectsGroupReadableKeyMaterial() throws Exception {
        var crypto=cipher("app-one");
        Files.setPosixFilePermissions(directory.resolve("identity.key"),PosixFilePermissions.fromString("rw-r-----"));
        assertThrows(SpeakerFailure.class,()->crypto.encrypt("token-fixture"));
    }
}
