package xiaozhi.modules.customer;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.LinkOption;
import java.util.Map;
import java.util.Base64;
import java.net.URI;
import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

@Component
final class CustomerOidcConfig {
    static final String ORIGIN = "https://bridge.hncig.cn:18443";
    static final URI CALLBACK = URI.create(ORIGIN + "/xiaozhi/customer/oidc/callback");
    final String issuer;
    final String clientId;
    final String clientSecret;
    private final byte[] encryptionKey;

    CustomerOidcConfig(@Value("${customer.oidc.config-file:}") String filename,
            @Value("${customer.oidc.encryption-key-file:}") String encryptionFilename) {
        if (filename.isBlank()) { issuer = clientId = clientSecret = null; encryptionKey = null; return; }
        try {
            Path file = privateFile(filename);
            Map<?,?> properties = new com.fasterxml.jackson.databind.ObjectMapper().readValue(Files.readString(file), Map.class);
            issuer = required(properties, "issuer");
            URI uri = URI.create(issuer);
            if (!issuer.equals("https://aimarket.hncig.cn/oidc")
                    && !(uri.getScheme().equals("http") && uri.getHost().equals("127.0.0.1"))) throw new IllegalArgumentException();
            clientId = required(properties, "client_id");
            clientSecret = required(properties, "client_secret");
            if (!CALLBACK.toString().equals(required(properties, "redirect_uri"))
                    || !(ORIGIN + "/xiaozhi/customer/oidc/backchannel-logout").equals(required(properties, "backchannel_logout_uri")))
                throw new IllegalArgumentException();
            Path keyFile = privateFile(encryptionFilename.isBlank() ? filename + ".key" : encryptionFilename);
            encryptionKey = Base64.getDecoder().decode(Files.readString(keyFile).strip());
            if (encryptionKey.length != 32) throw new IllegalArgumentException();
        } catch (Exception e) { throw new IllegalStateException("Customer OIDC requires a private, valid configuration file"); }
    }
    private static Path privateFile(String name) throws Exception {
        Path file = Path.of(name);
        if (!Files.isRegularFile(file, LinkOption.NOFOLLOW_LINKS) || Files.size(file) > 16384) throw new IllegalArgumentException();
        if (Files.getPosixFilePermissions(file).stream().anyMatch(p -> p.name().startsWith("GROUP_") || p.name().startsWith("OTHERS_")))
            throw new IllegalArgumentException();
        return file;
    }
    private static String required(Map<?,?> p, String name) {
        if (!(p.get(name) instanceof String value) || value.isBlank()) throw new IllegalArgumentException();
        return value;
    }
    void requireEnabled() { if (issuer == null) throw new CustomerFailure(503, "IDENTITY_NOT_CONFIGURED"); }
    String encrypt(String value) {
        requireEnabled();
        try {
            byte[] iv = new byte[12];
            new SecureRandom().nextBytes(iv);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(encryptionKey, "AES"), new GCMParameterSpec(128, iv));
            byte[] encrypted = cipher.doFinal(value.getBytes(StandardCharsets.UTF_8));
            return Base64.getEncoder().encodeToString(iv) + "." + Base64.getEncoder().encodeToString(encrypted);
        } catch (Exception e) { throw new CustomerFailure(503, "IDENTITY_STORAGE_UNAVAILABLE"); }
    }
    String decrypt(String value) {
        requireEnabled();
        try {
            String[] parts = value.split("\\.");
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, new SecretKeySpec(encryptionKey, "AES"),
                    new GCMParameterSpec(128, Base64.getDecoder().decode(parts[0])));
            return new String(cipher.doFinal(Base64.getDecoder().decode(parts[1])), StandardCharsets.UTF_8);
        } catch (Exception e) { throw new CustomerFailure(503, "IDENTITY_STORAGE_UNAVAILABLE"); }
    }
}
