package xiaozhi.modules.speaker;

/** Public stable error code. Never includes upstream bodies, tokens or SQL values. */
public final class SpeakerFailure extends RuntimeException {
    public final int status;
    public SpeakerFailure(int status, String code) { super(code); this.status = status; }
}
