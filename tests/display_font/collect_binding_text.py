"""Build test fixtures from the binding screen's actual displayed text."""
import json
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()
texts = set()
for method in ("CreateDeviceBindingUi", "UpdateBindingCountdown", "ShowDeviceBinding"):
    match = re.search(
        rf"void LcdDisplay::{method}\([^)]*\)\s*\{{(.*?)^\}}",
        source,
        re.DOTALL | re.MULTILINE,
    )
    if not match:
        raise SystemExit(f"Cannot find {method} in {sys.argv[1]}")
    for literal in re.findall(r'"(?:[^"\\]|\\.)*"', match[1]):
        text = json.loads(literal)
        if any(ord(char) > 127 for char in text):
            texts.add(text.replace("%02lld", "59"))
# Include every digit, since the countdown dynamically substitutes these.
texts.add("0123456789:")
if len(texts) < 10:
    raise SystemExit("Binding text extraction produced too few fixtures")
lines = ["// Generated from lcd_display.cc; do not edit.", "static const char* const binding_texts[] = {"]
lines.extend("    " + json.dumps(text, ensure_ascii=False) + "," for text in sorted(texts))
lines.append("};")
Path(sys.argv[2]).write_text("\n".join(lines) + "\n")
