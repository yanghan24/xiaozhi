"""Compile and exercise the actual application wake handlers without ESP-IDF."""

import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
application = (root / "main/application.cc").read_text()
start = application.index("void Application::HandleWakeWordDetectedEvent()")
end = application.index("void Application::HandleStateChangedEvent()", start)
handlers = application[start:end]

with tempfile.TemporaryDirectory(prefix="xiaozhi-wake-greeting-") as directory:
    build = Path(directory)
    (build / "wake_handlers.inc").write_text(handlers)
    binary = build / "wake_greeting_test"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-I", str(build),
        str(Path(__file__).with_name("greeting_test.cc")), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
