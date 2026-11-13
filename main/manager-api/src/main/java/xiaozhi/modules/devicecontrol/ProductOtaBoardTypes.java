package xiaozhi.modules.devicecontrol;

/** Resolve the firmware's family name only with an independently verified delivery variant. */
final class ProductOtaBoardTypes {
    private static final String TOUCH_LCD_4_3 = "waveshare-esp32-p4-wifi6-touch-lcd-4.3";
    private static final String REPORTED_FAMILY = "esp32-p4-wifi6-touch-lcd";

    private ProductOtaBoardTypes() { }

    static boolean matches(String imageBoard, String reportedBoard, String registeredBoard, boolean delivered) {
        if (!TOUCH_LCD_4_3.equals(imageBoard)) return false;
        if (registeredBoard != null && !TOUCH_LCD_4_3.equals(registeredBoard)) return false;
        if (TOUCH_LCD_4_3.equals(reportedBoard)) return true;
        return REPORTED_FAMILY.equals(reportedBoard) && delivered && TOUCH_LCD_4_3.equals(registeredBoard);
    }
}
