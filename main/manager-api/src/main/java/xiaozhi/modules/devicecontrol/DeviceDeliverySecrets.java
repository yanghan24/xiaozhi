package xiaozhi.modules.devicecontrol;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.Base64;
import java.util.HexFormat;
import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/** Pending delivery secrets are recoverable only by the authorized operator. */
final class DeviceDeliverySecrets {
    private static final SecureRandom RANDOM = new SecureRandom();
    private final byte[] key;

    DeviceDeliverySecrets(String filename) {
        try {
            Path path = Path.of(filename);
            if (!Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS) || Files.size(path) > 256
                    || Files.getPosixFilePermissions(path).stream().anyMatch(permission ->
                        permission.name().startsWith("GROUP_") || permission.name().startsWith("OTHERS_"))) {
                throw new IllegalArgumentException();
            }
            key = Base64.getDecoder().decode(Files.readString(path).strip());
            if (key.length != 32) throw new IllegalArgumentException();
        } catch (Exception invalid) {
            throw new IllegalStateException("Device delivery requires a private base64-encoded 32-byte encryption key");
        }
    }

    static String random() {
        byte[] bytes = new byte[32];
        RANDOM.nextBytes(bytes);
        return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);
    }

    static String hash(String value) {
        try {
            return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256")
                    .digest(value.getBytes(StandardCharsets.UTF_8)));
        } catch (java.security.NoSuchAlgorithmException impossible) {
            throw new IllegalStateException(impossible);
        }
    }

    static boolean sameHash(String left, String right) {
        return MessageDigest.isEqual(left.getBytes(StandardCharsets.US_ASCII), right.getBytes(StandardCharsets.US_ASCII));
    }

    String encrypt(String plaintext, String context) {
        try {
            byte[] iv = new byte[12];
            RANDOM.nextBytes(iv);
            Cipher cipher = cipher(Cipher.ENCRYPT_MODE, iv, context);
            return Base64.getEncoder().encodeToString(iv) + "."
                    + Base64.getEncoder().encodeToString(cipher.doFinal(plaintext.getBytes(StandardCharsets.US_ASCII)));
        } catch (Exception unavailable) {
            throw new ControlFailure(503, "DELIVERY_STORAGE_UNAVAILABLE");
        }
    }

    String decrypt(String encrypted, String context) {
        try {
            String[] parts = encrypted.split("\\.", -1);
            if (parts.length != 2) throw new IllegalArgumentException();
            byte[] iv = Base64.getDecoder().decode(parts[0]);
            if (iv.length != 12) throw new IllegalArgumentException();
            return new String(cipher(Cipher.DECRYPT_MODE, iv, context)
                    .doFinal(Base64.getDecoder().decode(parts[1])), StandardCharsets.US_ASCII);
        } catch (Exception unavailable) {
            throw new ControlFailure(503, "DELIVERY_STORAGE_UNAVAILABLE");
        }
    }

    private Cipher cipher(int mode, byte[] iv, String context) throws Exception {
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(mode, new SecretKeySpec(key, "AES"), new GCMParameterSpec(128, iv));
        cipher.updateAAD(context.getBytes(StandardCharsets.UTF_8));
        return cipher;
    }
}
