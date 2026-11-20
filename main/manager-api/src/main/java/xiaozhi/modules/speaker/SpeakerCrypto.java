package xiaozhi.modules.speaker;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.security.SecureRandom;
import java.util.Base64;
import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import org.springframework.stereotype.Component;

/** Backend-only encryption: identity tokens bind to the app; MCP configuration has its own context. */
@Component
public final class SpeakerCrypto {
    private final SpeakerSettings settings;
    private volatile byte[] key;
    public SpeakerCrypto(SpeakerSettings settings) { this.settings = settings; }
    private byte[] key() throws Exception {
        if (key != null) return key;
        synchronized (this) {
            if (key != null) return key;
            if (settings.encryptionKeyFile().isBlank()) throw new IllegalArgumentException();
            Path file = Path.of(settings.encryptionKeyFile());
            if (!Files.isRegularFile(file, LinkOption.NOFOLLOW_LINKS) || Files.size(file) > 128
                    || Files.getPosixFilePermissions(file).stream().anyMatch(p ->
                    p.name().startsWith("GROUP_") || p.name().startsWith("OTHERS_"))) throw new IllegalArgumentException();
            byte[] loaded = Base64.getDecoder().decode(Files.readString(file).strip());
            if (loaded.length != 32) throw new IllegalArgumentException();
            key = loaded;
            return key;
        }
    }
    private Cipher cipher(int mode, byte[] iv, String context) throws Exception {
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(mode, new SecretKeySpec(key(),"AES"), new GCMParameterSpec(128,iv));
        cipher.updateAAD(context.getBytes(StandardCharsets.UTF_8));
        return cipher;
    }
    public String encrypt(String value) {
        return encrypt(value, settings.issuer()+"\n"+settings.clientId());
    }
    public String encryptConfiguration(String value) { return "mcp1."+encrypt(value,"xiaozhi:mcp-configuration:v1"); }
    String encryptEnrollmentCredential(String value, String deviceId) {
        return encrypt(value, "xiaozhi:speaker-enrollment:v1\n" + deviceId);
    }

    String decryptEnrollmentCredential(String value, String deviceId) {
        return decrypt(value, "xiaozhi:speaker-enrollment:v1\n" + deviceId);
    }

    private String encrypt(String value,String context) {
        try {
            byte[] iv = new byte[12]; new SecureRandom().nextBytes(iv);
            return Base64.getEncoder().encodeToString(iv) + "." + Base64.getEncoder().encodeToString(
                    cipher(Cipher.ENCRYPT_MODE,iv,context).doFinal(value.getBytes(StandardCharsets.UTF_8)));
        } catch (Exception failure) { throw new SpeakerFailure(503,"CREDENTIAL_STORAGE_UNAVAILABLE"); }
    }
    public String decrypt(String value) {
        return decrypt(value,settings.issuer()+"\n"+settings.clientId());
    }
    public String decryptConfiguration(String value) {
        if(!value.startsWith("mcp1.")) throw new SpeakerFailure(503,"CREDENTIAL_STORAGE_UNAVAILABLE");
        return decrypt(value.substring(5),"xiaozhi:mcp-configuration:v1");
    }
    private String decrypt(String value,String context) {
        try {
            String[] parts = value.split("\\.",-1);
            if (parts.length != 2) throw new IllegalArgumentException();
            return new String(cipher(Cipher.DECRYPT_MODE,Base64.getDecoder().decode(parts[0]),context)
                    .doFinal(Base64.getDecoder().decode(parts[1])),StandardCharsets.UTF_8);
        } catch (Exception failure) { throw new SpeakerFailure(503,"CREDENTIAL_STORAGE_UNAVAILABLE"); }
    }
}
