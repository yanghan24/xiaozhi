import ast
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class StaticRulesTest(unittest.TestCase):
    def test_package_is_python38_syntax(self):
        for path in sorted((ROOT / "identity_check").glob("*.py")):
            with self.subTest(module=path.name):
                ast.parse(path.read_text(encoding="utf-8"), feature_version=(3, 8))

    def test_no_business_network_clients_or_fixed_rounds(self):
        forbidden = {"urllib", "http", "socket", "requests", "httpx", "ftplib", "paramiko"}
        for path in sorted((ROOT / "identity_check").glob("*.py")):
            text = path.read_text(encoding="utf-8")
            tree = ast.parse(text)
            with self.subTest(module=path.name):
                self.assertIsNone(re.search(r"\b20\d{2}-\d{2}-\d{2}\b", text))
                self.assertNotIn("issue11", text)
                self.assertNotIn("issue29", text)
                for node in ast.walk(tree):
                    if isinstance(node, ast.Import):
                        self.assertFalse({alias.name.split(".")[0] for alias in node.names} & forbidden)
                    if isinstance(node, ast.ImportFrom) and node.module:
                        self.assertNotIn(node.module.split(".")[0], forbidden)
                    if isinstance(node, ast.Call):
                        for argument in node.keywords:
                            if argument.arg == "shell":
                                self.assertIsInstance(argument.value, ast.Constant)
                                self.assertIs(argument.value.value, False)

    def test_script_can_start_isolated(self):
        import subprocess
        import sys
        result = subprocess.run([sys.executable, "-I", str(ROOT / "check-identity.py"), "--help"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0)
        self.assertIn("two-account", result.stdout)
        self.assertNotIn("Traceback", result.stderr)
