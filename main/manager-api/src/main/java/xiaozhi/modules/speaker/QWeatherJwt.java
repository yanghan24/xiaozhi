package xiaozhi.modules.speaker;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.KeyFactory;
import java.security.PrivateKey;
import java.security.Signature;
import java.security.spec.PKCS8EncodedKeySpec;
import java.time.Instant;
import java.util.Base64;
import java.util.Map;

final class QWeatherJwt {
    private static final Base64.Encoder BASE64_URL = Base64.getUrlEncoder().withoutPadding();

    private QWeatherJwt() {}

    static boolean validPrivateKey(String pem) {
        if (pem.length() > 4096) return false;
        try {
            privateKey(pem);
            return true;
        } catch (GeneralSecurityException | IllegalArgumentException invalid) {
            return false;
        }
    }

    static String sign(ObjectMapper json, SpeakerThirdPartyServices.QWeatherConfiguration config, Instant now)
            throws java.io.IOException, GeneralSecurityException {
        long issued = now.getEpochSecond() - 30;
        String header = encode(json.writeValueAsBytes(Map.of("alg", "EdDSA", "kid", config.keyId())));
        String claims = encode(json.writeValueAsBytes(Map.of("iss", config.issuer(), "sub", config.project(),
                "iat", issued, "exp", issued + 900)));
        String unsigned = header + "." + claims;
        var signer = Signature.getInstance("Ed25519");
        signer.initSign(privateKey(config.privateKey()));
        signer.update(unsigned.getBytes(StandardCharsets.US_ASCII));
        return unsigned + "." + encode(signer.sign());
    }

    private static PrivateKey privateKey(String pem) throws GeneralSecurityException {
        var base64 = pem.replace("-----BEGIN PRIVATE KEY-----", "").replace("-----END PRIVATE KEY-----", "")
                .replaceAll("\\s+", "");
        return KeyFactory.getInstance("Ed25519").generatePrivate(new PKCS8EncodedKeySpec(Base64.getDecoder().decode(base64)));
    }

    private static String encode(byte[] value) {
        return BASE64_URL.encodeToString(value);
    }
}
