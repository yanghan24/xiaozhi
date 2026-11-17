"""Offline T29 tests for bounded private record input.

The tests use only synthetic values.  They cover the security boundary that a
round record is a private regular file inside ``record_root``: duplicate JSON
keys, oversized files, missing files, paths outside the root and symlinks must
fail with a fixed error code and must never echo the path, handle or raw body.
"""

import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from identity_check.errors import CheckError
from identity_check.records import (
    MAX_BYTES,
    RoundInput,
    decode_json,
    load_round,
    private_json,
    record_fingerprints,
)


VALID_DOCUMENT = {
    "schema": "xiaozhi-identity-round-v1",
    "request_handle": "SyntheticHandle_1234",
}
VALID_HANDLE = VALID_DOCUMENT["request_handle"]
# Same structure as the existing server round records, with synthetic values.
LEGACY_DOCUMENT = {
    "url": "https://bind.example.invalid/xiaozhi/bind/" + VALID_HANDLE,
    "handle": VALID_HANDLE,
    "created_at": 1700000000,
    "expires_at": 1700000600,
    "baseline_receipts": "0",
    "baseline_log_lines": 0,
}


def write_file(path, content, mode=0o600):
    """Write bytes and apply POSIX private mode when the host has mode bits."""
    if isinstance(content, str):
        content = content.encode("utf-8")
    path.write_bytes(content)
    if os.name == "posix":
        os.chmod(str(path), mode)
    return path


def write_json(path, value, mode=0o600):
    return write_file(path, json.dumps(value).encode("utf-8"), mode)


class RecordTestBase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / "record.json"

    def assert_code(self, code, function, *args, **kwargs):
        with self.assertRaises(CheckError) as caught:
            function(*args, **kwargs)
        self.assertEqual(caught.exception.code, code)
        self.assertEqual(str(caught.exception), code)
        return caught.exception


class PrivateJsonBoundaryTest(RecordTestBase):
    def test_t29_private_json_reads_regular_file_and_hashes_exact_bytes(self):
        raw = json.dumps(VALID_DOCUMENT).encode("utf-8")
        write_file(self.path, raw)

        data, fingerprint = private_json(self.path, self.root)

        self.assertEqual(data, VALID_DOCUMENT)
        self.assertEqual(fingerprint, hashlib.sha256(raw).hexdigest())

    def test_t29_exact_byte_limit_is_accepted(self):
        prefix = b'{"padding":"'
        suffix = b'"}'
        filler = b"a" * (MAX_BYTES - len(prefix) - len(suffix))
        raw = prefix + filler + suffix
        self.assertEqual(len(raw), MAX_BYTES)
        write_file(self.path, raw)

        data, fingerprint = private_json(self.path, self.root)

        self.assertEqual(len(data["padding"]), len(filler))
        self.assertEqual(fingerprint, hashlib.sha256(raw).hexdigest())

    def test_t29_duplicate_keys_are_rejected_at_any_depth(self):
        for name, raw in (
            ("top-level", b'{"schema":"synthetic","schema":"replaced"}'),
            ("nested", b'{"outer":{"value":1,"value":2}}'),
        ):
            with self.subTest(case=name):
                write_file(self.path, raw)
                self.assert_code("INPUT_DUPLICATE_FIELD", private_json, self.path, self.root)

    def test_t29_oversized_file_is_rejected_before_reading(self):
        with self.path.open("wb") as stream:
            stream.truncate(MAX_BYTES + 1)
        if os.name == "posix":
            os.chmod(str(self.path), 0o600)

        self.assert_code("INPUT_TOO_LARGE", private_json, self.path, self.root)

    def test_t29_missing_file_is_unavailable_without_echoing_path(self):
        missing = self.root / "sensitive-missing-record.json"

        error = self.assert_code("INPUT_UNAVAILABLE", private_json, missing, self.root)

        self.assertNotIn(str(missing), str(error))
        self.assertNotIn(missing.name, str(error))
        self.assertNotIn(missing.name, repr(error))

    def test_t29_path_outside_root_is_rejected_without_echoing_path(self):
        outside_temp = tempfile.TemporaryDirectory()
        self.addCleanup(outside_temp.cleanup)
        outside = Path(outside_temp.name) / "sensitive-outside-record.json"
        write_file(outside, json.dumps(VALID_DOCUMENT))

        error = self.assert_code("INPUT_OUTSIDE_ROOT", private_json, outside, self.root)

        self.assertNotIn(str(outside), str(error))
        self.assertNotIn(outside.name, repr(error))

    def test_t29_symlink_record_is_rejected(self):
        target = write_json(self.root / "target.json", VALID_DOCUMENT)
        link = self.root / "link.json"
        try:
            link.symlink_to(target)
        except (OSError, NotImplementedError):
            self.skipTest("the host does not permit creating test symlinks")

        self.assert_code("INPUT_SYMLINK", private_json, link, self.root)

    def test_t29_symlinked_directory_component_is_rejected(self):
        real = self.root / "real"
        real.mkdir()
        write_json(real / "record.json", VALID_DOCUMENT)
        link_dir = self.root / "linked"
        try:
            link_dir.symlink_to(real, target_is_directory=True)
        except (OSError, NotImplementedError):
            self.skipTest("the host does not permit creating directory symlinks")

        self.assert_code("INPUT_SYMLINK", private_json, link_dir / "record.json", self.root)

    def test_t29_invalid_json_forms_have_fixed_codes(self):
        for name, raw, code in (
            ("empty", b"", "INPUT_INVALID_JSON"),
            ("invalid-utf8", b"\xff\xfe", "INPUT_INVALID_JSON"),
            ("nan", b'{"value":NaN}', "INPUT_INVALID_JSON"),
            ("infinity", b'{"value":Infinity}', "INPUT_INVALID_JSON"),
            ("top-level-list", b"[]", "INPUT_INVALID_STRUCTURE"),
            ("top-level-string", b'"synthetic"', "INPUT_INVALID_STRUCTURE"),
            ("top-level-number", b"1", "INPUT_INVALID_STRUCTURE"),
        ):
            with self.subTest(case=name):
                write_file(self.path, raw)
                self.assert_code(code, private_json, self.path, self.root)

    def test_t29_raw_json_and_path_never_appear_in_errors(self):
        secret = "SyntheticSecretHandle_0123456789"
        raw = ('{"schema":"xiaozhi-identity-round-v1","request_handle":"%s"}'
               % secret).encode("utf-8")[:-1]
        write_file(self.path, raw)

        error = self.assert_code("INPUT_INVALID_JSON", private_json, self.path, self.root)

        for text in (str(error), repr(error)):
            self.assertNotIn(secret, text)
            self.assertNotIn(str(self.path), text)
            self.assertNotIn(self.path.name, text)
        self.assertNotIn(secret, repr(error.__context__))

    def test_t29_check_error_sanitizes_unsafe_codes(self):
        error = CheckError("password=synthetic\nC:/private/record.json")

        self.assertEqual(error.code, "CHECK_FAILED")
        self.assertEqual(str(error), "CHECK_FAILED")
        self.assertNotIn("password", repr(error))
        self.assertNotIn("record.json", repr(error))


class LoadRoundTest(RecordTestBase):
    def test_t29_valid_round_keeps_secrets_out_of_repr(self):
        write_json(self.path, VALID_DOCUMENT)

        record = load_round(self.path, "A", self.root)

        self.assertIsInstance(record, RoundInput)
        self.assertEqual(record.label, "A")
        self.assertEqual(record.handle, VALID_HANDLE)
        self.assertEqual(record.path, self.path)
        self.assertEqual(len(record.fingerprint), 64)
        shown = repr(record)
        self.assertNotIn(VALID_HANDLE, shown)
        self.assertNotIn(record.fingerprint, shown)
        self.assertNotIn(str(self.path), shown)

    def test_t29_label_is_limited_to_a_and_b(self):
        self.assert_code("INPUT_LABEL_INVALID", load_round,
                         self.root / "missing.json", "C", self.root)
        self.assert_code("INPUT_LABEL_INVALID", load_round,
                         self.root / "missing.json", "a", self.root)

    def test_t29_round_schema_and_keys_are_exact(self):
        for name, value in (
            ("wrong-schema", {"schema": "other", "request_handle": VALID_HANDLE}),
            ("extra-key", {"schema": VALID_DOCUMENT["schema"],
                           "request_handle": VALID_HANDLE, "extra": "synthetic"}),
            ("legacy-extra-key", dict(LEGACY_DOCUMENT, extra="synthetic")),
            ("legacy-without-handle", {"url": "https://example.invalid/bind/x",
                                       "expires_at": 1}),
            ("device-request-record", {"url": "https://example.invalid/bind/x",
                                       "expires_at": 1, "device_id": "synthetic",
                                       "handle": VALID_HANDLE}),
        ):
            with self.subTest(case=name):
                write_json(self.path, value)
                self.assert_code("RECORD_FORMAT_UNSUPPORTED", load_round,
                                 self.path, "A", self.root)

    def test_existing_server_round_record_supplies_only_the_handle(self):
        for name, value in (
            ("full", LEGACY_DOCUMENT),
            ("minimal", {"url": LEGACY_DOCUMENT["url"], "handle": VALID_HANDLE,
                         "expires_at": LEGACY_DOCUMENT["expires_at"]}),
        ):
            with self.subTest(case=name):
                write_json(self.path, value)
                record = load_round(self.path, "A", self.root)
                self.assertEqual(VALID_HANDLE, record.handle)
                self.assertNotIn(VALID_HANDLE, repr(record))

    def test_legacy_handle_is_validated_like_the_tool_format(self):
        write_json(self.path, dict(LEGACY_DOCUMENT, handle="bad handle SyntheticSecret"))
        error = self.assert_code("RECORD_HANDLE_INVALID", load_round,
                                 self.path, "A", self.root)
        self.assertNotIn("SyntheticSecret", str(error))

    def test_t29_handle_must_match_bounded_character_class(self):
        values = (
            ("too-short", "A" * 15),
            ("too-long", "A" * 65),
            ("space", "Synthetic Handle"),
            ("dot", "Synthetic.Handle.1234"),
            ("non-string", 1234567890123456),
        )
        for name, handle in values:
            with self.subTest(case=name):
                write_json(self.path, {"schema": VALID_DOCUMENT["schema"],
                                       "request_handle": handle})
                self.assert_code("RECORD_HANDLE_INVALID", load_round,
                                 self.path, "A", self.root)

    def test_t29_invalid_handle_is_not_echoed(self):
        handle = "bad handle with spaces SyntheticSecret"
        write_json(self.path, {"schema": VALID_DOCUMENT["schema"],
                               "request_handle": handle})

        error = self.assert_code("RECORD_HANDLE_INVALID", load_round,
                                 self.path, "A", self.root)

        self.assertNotIn(handle, str(error))
        self.assertNotIn(handle, repr(error))

    def test_t29_record_fingerprints_are_label_keyed(self):
        records = {
            "A": RoundInput("A", "SyntheticHandleA_12", "a" * 64, Path("synthetic-a")),
            "B": RoundInput("B", "SyntheticHandleB_12", "b" * 64, Path("synthetic-b")),
        }

        self.assertEqual(record_fingerprints(records), {"A": "a" * 64, "B": "b" * 64})


@unittest.skipUnless(os.name == "posix",
                     "POSIX 0600 permission bits cannot be enforced on Windows")
class PrivatePermissionTest(RecordTestBase):
    def test_t29_group_or_other_access_is_not_private(self):
        for mode in (0o644, 0o640, 0o604, 0o660, 0o606):
            with self.subTest(mode=oct(mode)):
                write_json(self.path, VALID_DOCUMENT, mode=mode)
                self.assert_code("INPUT_NOT_PRIVATE", private_json, self.path, self.root)

    def test_t29_owner_only_read_write_and_read_only_are_private(self):
        for mode in (0o600, 0o400):
            with self.subTest(mode=oct(mode)):
                write_json(self.path, VALID_DOCUMENT, mode=mode)
                data, _ = private_json(self.path, self.root)
                self.assertEqual(data, VALID_DOCUMENT)


if __name__ == "__main__":
    unittest.main()
