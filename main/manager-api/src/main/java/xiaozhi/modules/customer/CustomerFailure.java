package xiaozhi.modules.customer;

/** Public errors contain stable codes only, never upstream bodies or credentials. */
final class CustomerFailure extends RuntimeException {
    final int status;
    CustomerFailure(int status, String code) { super(code); this.status = status; }
}
