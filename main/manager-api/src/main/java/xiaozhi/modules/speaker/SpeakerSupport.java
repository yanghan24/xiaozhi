package xiaozhi.modules.speaker;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.Base64;
import java.util.HexFormat;
import java.util.Map;

public final class SpeakerSupport {
    private static final SecureRandom RANDOM = new SecureRandom();
    private SpeakerSupport() {}
    public static String token() {
        byte[] value = new byte[32]; RANDOM.nextBytes(value);
        return Base64.getUrlEncoder().withoutPadding().encodeToString(value);
    }
    public static String hash(String value) {
        try {
            return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256")
                    .digest(value.getBytes(StandardCharsets.UTF_8)));
        } catch (java.security.NoSuchAlgorithmException e) { throw new IllegalStateException(e); }
    }
    public static boolean yes(Object value) {
        return Boolean.TRUE.equals(value) || value instanceof Number n && n.intValue() == 1;
    }
    public static long number(Map<String, Object> row, String key) {
        return ((Number) row.get(key)).longValue();
    }
    public static boolean online(Object lastSeen, long now) {
        if (!(lastSeen instanceof Number n)) return false;
        long time = n.longValue();
        return time > 0 && time <= now && now - time < 75_000;
    }
}
