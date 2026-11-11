package xiaozhi.modules.devicecontrol;

import java.util.Map;

final class ControlFailure extends RuntimeException {
    final int status;
    final String code;

    ControlFailure(int status, String code) {
        super(code);
        this.status = status;
        this.code = code;
    }

    Map<String, Object> body() {
        return Map.of("version", 1, "code", code, "message", code);
    }
}
