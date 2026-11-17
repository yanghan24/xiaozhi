package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.*;
import org.junit.jupiter.api.Test;

class ProductOtaBoardTypesTest {
    private static final String PRECISE = "waveshare-esp32-p4-wifi6-touch-lcd-4.3";
    private static final String FAMILY = "esp32-p4-wifi6-touch-lcd";

    @Test void familyNameNeedsAnIndependentCompletedDeliveryOfThePreciseVariant() {
        assertTrue(ProductOtaBoardTypes.matches(PRECISE, FAMILY, PRECISE, true));
        assertFalse(ProductOtaBoardTypes.matches(PRECISE, FAMILY, PRECISE, false));
        assertFalse(ProductOtaBoardTypes.matches(PRECISE, FAMILY, null, false));
        assertFalse(ProductOtaBoardTypes.matches(PRECISE, FAMILY, "waveshare-esp32-p4-wifi6-touch-lcd-7", true));
        assertFalse(ProductOtaBoardTypes.matches(FAMILY, FAMILY, PRECISE, true));
    }

    @Test void exactRuntimeNameCannotOverrideAConflictingRegistration() {
        assertTrue(ProductOtaBoardTypes.matches(PRECISE, PRECISE, null, false));
        assertFalse(ProductOtaBoardTypes.matches(PRECISE, PRECISE, "unknown", true));
    }

    @Test void unknownAndMissingNamesFailClosedWithoutThrowing() {
        assertFalse(ProductOtaBoardTypes.matches(null, FAMILY, PRECISE, true));
        assertFalse(ProductOtaBoardTypes.matches(PRECISE, null, PRECISE, true));
        assertFalse(ProductOtaBoardTypes.matches(PRECISE, "unknown", PRECISE, true));
    }
}
