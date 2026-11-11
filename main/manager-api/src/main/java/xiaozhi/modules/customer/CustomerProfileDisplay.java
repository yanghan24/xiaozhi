package xiaozhi.modules.customer;

import java.nio.charset.StandardCharsets;
import java.util.Map;

/** Select bounded display text from an already verified owner's UserInfo. */
final class CustomerProfileDisplay {
    private CustomerProfileDisplay() {}

    static String name(Map<?, ?> profile) {
        for (String field : new String[] {"nickname", "name", "preferred_username"}) {
            String value = text(profile.get(field));
            if (value != null) return value;
        }
        return null;
    }

    private static String text(Object candidate) {
        if (candidate instanceof String value && !value.isBlank()
                && value.getBytes(StandardCharsets.UTF_8).length <= 256
                && value.codePoints().noneMatch(Character::isISOControl)) return value;
        return null;
    }
}
