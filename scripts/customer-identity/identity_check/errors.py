"""Safe error codes: never expose exception bodies from external tools."""
import re


class CheckError(Exception):
    def __init__(self, code):
        self.code = code if isinstance(code, str) and re.fullmatch(r"[A-Z][A-Z0-9_]{1,63}", code) else "CHECK_FAILED"
        super().__init__(self.code)
