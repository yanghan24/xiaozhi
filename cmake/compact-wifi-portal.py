"""Remove formatting indentation from the locked Wi-Fi portal's inline code."""

import re
import sys
from pathlib import Path


INLINE_CODE = re.compile(
    r"(?P<opening><(?P<tag>script|style)\b[^>]*>)(?P<code>.*?)(?P<closing></(?P=tag)>)",
    re.IGNORECASE | re.DOTALL,
)


def compact_code(code: str) -> str:
    # Retain all line breaks, including JavaScript's implicit statement breaks.
    # Preserve raw template text and any escaped multiline string verbatim.
    if re.search(r"\\\r?\n", code):
        return code
    strip_indent = re.compile(r"(?m)(?<=\n)[ \t]+").sub
    if "`" not in code:
        return strip_indent("", code)
    first = code.rfind("\n", 0, code.index("`")) + 1
    last = code.find("\n", code.rindex("`"))
    if last == -1:
        last = len(code)
    return strip_indent("", code[:first]) + code[first:last] + strip_indent("", code[last:])


def main() -> None:
    source, output = map(Path, sys.argv[1:])
    html = source.read_bytes().decode("utf-8")

    def compact(match: re.Match[str]) -> str:
        return match["opening"] + compact_code(match["code"]) + match["closing"]

    compacted = INLINE_CODE.sub(compact, html).encode("utf-8")
    if not output.exists() or output.read_bytes() != compacted:
        output.write_bytes(compacted)


if __name__ == "__main__":
    main()
