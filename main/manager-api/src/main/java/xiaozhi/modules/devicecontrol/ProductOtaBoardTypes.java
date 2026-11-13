package xiaozhi.modules.devicecontrol;

import java.util.Set;

/** Canonical board identity and the historical name accepted in firmware metadata. */
final class ProductOtaBoardTypes {
    static final String CANONICAL = "esp32-p4-wifi6-touch-lcd";
    static final String LEGACY_WAVESHARE = "waveshare-esp32-p4-wifi6-touch-lcd-4.3";
    private static final Set<String> ALIASES = Set.of(CANONICAL, LEGACY_WAVESHARE);

    private ProductOtaBoardTypes() { }

    /** Returns the stable stored identity, or null for an unsupported board name. */
    static String canonical(String board) {
        return ALIASES.contains(board) ? CANONICAL : null;
    }
}
