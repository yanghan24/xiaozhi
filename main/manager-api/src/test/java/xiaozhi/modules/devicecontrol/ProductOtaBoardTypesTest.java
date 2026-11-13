package xiaozhi.modules.devicecontrol;

import static org.junit.jupiter.api.Assertions.*;
import org.junit.jupiter.api.Test;

class ProductOtaBoardTypesTest {
    @Test void mapsCurrentAndLegacyNamesToOneCanonicalIdentity() {
        assertEquals(ProductOtaBoardTypes.CANONICAL,
                ProductOtaBoardTypes.canonical(ProductOtaBoardTypes.CANONICAL));
        assertEquals(ProductOtaBoardTypes.CANONICAL,
                ProductOtaBoardTypes.canonical(ProductOtaBoardTypes.LEGACY_WAVESHARE));
    }

    @Test void rejectsUnknownOrMissingBoardNames() {
        assertNull(ProductOtaBoardTypes.canonical("other-board"));
        assertNull(ProductOtaBoardTypes.canonical(null));
    }
}
