import unittest

from identity_check import sql
from identity_check.errors import CheckError


def _schema_rows(version="8.0.36"):
    rows = [{"kind": "version", "version": version}]
    types = {"string": "varchar", "bigint": "bigint", "integer": "bigint",
             "text": "text", "boolean": "tinyint"}
    for table in sorted(sql.SCHEMA_REQUIRED):
        for column, family in sql.SCHEMA_REQUIRED[table]:
            rows.append({"kind": "column", "table": table, "name": column,
                         "type": types[family]})
    return rows


class HandleValidationTest(unittest.TestCase):
    def test_valid_handles_are_accepted(self):
        for value in ("A" * 16, "abcDEF0123456789xyz", "-_" * 8, "a" * 64):
            with self.subTest(length=len(value)):
                self.assertEqual(sql.validate_handle(value), value)

    def test_malicious_or_malformed_handles_are_rejected(self):
        values = ("", "short", "a" * 65, "abc' OR 1=1 --xyz",
                  "abc\\defghijklmnop", "abc defghijklmnop", "abc\x00defghijklmnop",
                  "中文中文中文中文中文中文", None, 12345)
        for value in values:
            with self.subTest(value=value):
                with self.assertRaises(CheckError) as caught:
                    sql.validate_handle(value)
                self.assertEqual(caught.exception.code, "HANDLE_INVALID")

    def test_other_identifiers_and_paths(self):
        self.assertEqual(sql.validate_database("db_1"), "db_1")
        self.assertEqual(sql.validate_container("entry-1.test"), "entry-1.test")
        self.assertEqual(sql.validate_log_path("/var/log/nginx/notify.log"),
                         "/var/log/nginx/notify.log")
        bad = ("", "db-name", "db;drop", "../db", "a" * 65)
        for value in bad:
            with self.subTest(value=value):
                with self.assertRaises(CheckError):
                    sql.validate_database(value)
        for value in ("relative/path", "/var/log/../secret", "/var/log/a b", "/var/log/x\x00"):
            with self.subTest(value=value):
                with self.assertRaises(CheckError):
                    sql.validate_log_path(value)


class EncodingTest(unittest.TestCase):
    def test_strings_are_hex_literals(self):
        self.assertEqual(sql.literal("abc"), "_utf8mb4 X'616263' COLLATE utf8mb4_bin")
        self.assertEqual(sql.ascii_literal("abc"), "_ascii X'616263' COLLATE ascii_bin")
        self.assertEqual(sql.literal("中"), "_utf8mb4 X'E4B8AD' COLLATE utf8mb4_bin")
        with self.assertRaises(CheckError):
            sql.ascii_literal("中")

    def test_integers_are_bounded(self):
        self.assertEqual(sql.integer(0), "0")
        self.assertEqual(sql.integer(42, 1, 100), "42")
        for value in (-1, True, 1.0, sql.MAX_MS + 1, "1"):
            with self.subTest(value=value):
                with self.assertRaises(CheckError):
                    sql.integer(value)

    def test_issuer_validation(self):
        self.assertEqual(sql.validate_issuer("https://issuer.example/oidc"),
                         "https://issuer.example/oidc")
        for value in ("http://issuer.example/oidc", "https://", "https://a b",
                      "https://issuer.example/" + "a" * 300, None):
            with self.subTest(value=value):
                with self.assertRaises(CheckError):
                    sql.validate_issuer(value)


class ScriptTest(unittest.TestCase):
    HANDLE = "AbCdEf0123456789xyzA"

    def test_observation_script_uses_bounded_hex_encoding(self):
        script = sql.build_observation_script(
            "db_name", "https://issuer.example/oidc", {"A": self.HANDLE},
            1700000000000, 1700001000000)
        self.assertNotIn(self.HANDLE, script)
        self.assertIn(sql.ascii_literal(self.HANDLE), script)
        self.assertIn("LIMIT 51", script)
        self.assertIn("LIMIT 501", script)
        self.assertIn("ORDER BY f.creation_order", script)
        self.assertIn("SET SESSION MAX_EXECUTION_TIME = 5000", script)
        self.assertIn("SET SESSION lock_wait_timeout = 5", script)
        self.assertIn("'issuer_matches'", script)
        self.assertIn("START TRANSACTION READ ONLY, WITH CONSISTENT SNAPSHOT;", script)
        self.assertIn("COMMIT;", script)

    def test_no_window_script_has_no_window_probes(self):
        script = sql.build_observation_script(
            "db_name", "https://issuer.example/oidc", {"A": self.HANDLE})
        self.assertNotIn("receipt_window", script)
        self.assertNotIn("'kind','matched'", script)
        self.assertIn("'matched_before_window',0", script)
        self.assertIn("'matched_in_window',0", script)

    def test_two_record_script_adds_pair_matrix(self):
        script = sql.build_observation_script(
            "db_name", "https://issuer.example/oidc",
            {"A": self.HANDLE, "B": "zzzzzzzzzzzzzzzzzzzz"})
        self.assertIn("'kind','pair'", script)
        self.assertIn("LIMIT 2501", script)

    def test_baseline_probe_counts_receipts_after_observation(self):
        script = sql.build_observation_script(
            "db_name", "https://issuer.example/oidc", {"A": self.HANDLE},
            1700000000000, 1700001000000, since_ms=1699990000000)
        self.assertIn("'kind','since_baseline'", script)
        self.assertIn("received_at_ms > 1699990000000", script)
        script = sql.build_observation_script(
            "db_name", "https://issuer.example/oidc", {"A": self.HANDLE})
        self.assertNotIn("since_baseline", script)

    def test_bad_builder_arguments_fail_closed(self):
        cases = (
            ("db_name", "https://issuer.example", {"B": self.HANDLE}, None, None),
            ("db_name", "https://issuer.example", {}, None, None),
            ("db_name", "https://issuer.example", {"A": "bad handle"},
             None, None),
            ("db_name", "https://issuer.example", {"A": self.HANDLE},
             10, None),
            ("db_name", "https://issuer.example", {"A": self.HANDLE},
             10, 10),
            ("bad-db", "https://issuer.example", {"A": self.HANDLE}, None, None),
        )
        for args in cases:
            with self.subTest(args=args):
                with self.assertRaises(CheckError):
                    sql.build_observation_script(*args)

    def test_schema_script_covers_all_six_changelog_tables(self):
        script = sql.build_schema_script("db_name")
        self.assertIn("information_schema.COLUMNS", script)
        self.assertIn("VERSION()", script)
        self.assertIn("LIMIT 501", script)
        self.assertIn("SET SESSION lock_wait_timeout = 5", script)
        for table in sql.SCHEMA_REQUIRED:
            self.assertIn(sql.literal(table), script)


class ReadOnlyGuardTest(unittest.TestCase):
    def test_generated_scripts_pass(self):
        sql.assert_read_only(sql.build_schema_script("db_name"))
        sql.assert_read_only(sql.build_observation_script(
            "db_name", "https://issuer.example", {"A": "A" * 16}))

    def test_exact_transaction_grammar_is_accepted(self):
        sql.assert_read_only(
            "START TRANSACTION READ ONLY, WITH CONSISTENT SNAPSHOT; SELECT 1; COMMIT;")

    def test_write_and_persistence_statements_are_rejected(self):
        scripts = (
            "SELECT 1; DELETE FROM customer_identity_flow;",
            "UPDATE customer_identity_flow SET status='SESSION_ENDED';",
            "INSERT INTO customer_logout_receipt VALUES (1);",
            "REPLACE INTO x VALUES (1);",
            "SELECT 1 FOR UPDATE;",
            "SELECT 1 FOR SHARE;",
            "SELECT 1 LOCK IN SHARE MODE;",
            "SELECT 1 INTO OUTFILE '/tmp/x';",
            "SELECT 1 INTO DUMPFILE '/tmp/x';",
            "SELECT 1 INTO @value;",
            "SELECT @value := 1;",
            "SELECT GET_LOCK('synthetic', 1);",
            "SELECT RELEASE_LOCK('synthetic');",
            "SELECT LOAD_FILE('/etc/passwd');",
            "SELECT SLEEP(10);",
            "SELECT BENCHMARK(1000000, MD5('x'));",
            "START TRANSACTION READ WRITE;",
            "SET GLOBAL max_connections = 1;",
            "/*!50000 DROP TABLE x */ SELECT 1;",
            "SELECT 1; -- hidden",
            "SELECT 1 /* trailing */;",
            "SELECT 1 INTO x",
        )
        for script in scripts:
            with self.subTest(script=script):
                with self.assertRaises(CheckError) as caught:
                    sql.assert_read_only(script)
                self.assertEqual(caught.exception.code, "SQL_READ_ONLY_GUARD")

    def test_column_names_do_not_look_like_keywords(self):
        sql.assert_read_only(
            "SELECT f.created_at_ms, a.updated_at_ms, f.identity_issued_at_ms "
            "FROM t f LIMIT 1;")


class SchemaParseTest(unittest.TestCase):
    def test_valid_schema_and_version(self):
        rows = _schema_rows()
        self.assertEqual(sql.schema_mismatches(rows), [])
        self.assertEqual(sql.version_major(rows), 8)

    def test_missing_or_wrong_typed_columns_are_reported(self):
        rows = _schema_rows()
        rows = [row for row in rows
                if not (row.get("table") == "customer_logout_receipt"
                        and row.get("name") == "sid")]
        self.assertIn(("customer_logout_receipt", "sid"), sql.schema_mismatches(rows))
        rows = _schema_rows()
        for row in rows:
            if row.get("table") == "customer_identity_flow" and row.get("name") == "status":
                row["type"] = "int"
        self.assertIn(("customer_identity_flow", "status"), sql.schema_mismatches(rows))

    def test_version_major_is_parsed_without_assuming(self):
        self.assertEqual(sql.version_major(_schema_rows("5.7.44")), 5)
        self.assertEqual(sql.version_major(_schema_rows("8.4.0")), 8)
        self.assertIsNone(sql.version_major([]))

    def test_json_rows_are_strict_and_never_echoed(self):
        rows = sql.parse_json_rows(b'{"kind":"observed","value":1}\n\n')
        self.assertEqual(rows, [{"kind": "observed", "value": 1}])
        for raw in (b"not json", b'["kind"]', b'{"value":1}', b"\xff"):
            with self.subTest(raw=raw):
                with self.assertRaises(CheckError) as caught:
                    sql.parse_json_rows(raw)
                self.assertEqual(caught.exception.code, "DATABASE_RESPONSE_INVALID")

    def test_mysql_errors_map_to_fixed_codes(self):
        cases = {
            b"ERROR 1045 (28000): denied": "MYSQL_AUTH_FAILED",
            b"ERROR 1064 (42000): syntax": "MYSQL_SYNTAX_ERROR",
            b"ERROR 1792 (25006): read only": "MYSQL_READ_ONLY_VIOLATION",
            b"ERROR 3024 (HY000): max execution": "MYSQL_TIMEOUT",
            b"ERROR 9999 (HY000): unknown": "MYSQL_COMMAND_FAILED",
            b"": "MYSQL_CREDENTIALS_MISSING",
            b"warning only": "MYSQL_COMMAND_FAILED",
        }
        for stderr, expected in cases.items():
            with self.subTest(stderr=stderr):
                self.assertEqual(sql.mysql_error_name(stderr), expected)

    def test_status_allowlists(self):
        self.assertEqual(sql.normalize_flow_status("IDENTITY_VERIFIED"),
                         "IDENTITY_VERIFIED")
        self.assertEqual(sql.normalize_flow_status("sensitive"), "UNKNOWN")
        self.assertEqual(sql.normalize_authorization_status("ACTIVE"), "ACTIVE")
        self.assertEqual(sql.normalize_authorization_status("NONE"), "NONE")
        self.assertEqual(sql.normalize_authorization_status("sensitive"), "UNKNOWN")


class ReceiptSemanticsTest(unittest.TestCase):
    ISSUER = "https://issuer.example/oidc"

    def _match(self, **overrides):
        values = {
            "flow_issuer": self.ISSUER, "flow_sid": "sid-a", "flow_subject": "sub-a",
            "flow_created_at_ms": 1000999, "receipt_issuer": self.ISSUER,
            "receipt_sid": "sid-a", "receipt_subject": "sub-a",
            "receipt_issued_at_ms": 1000000, "receipt_received_at_ms": 1001000,
        }
        values.update(overrides)
        return sql.receipt_matches(**values)

    def test_sid_takes_priority_and_never_falls_back_to_subject(self):
        self.assertFalse(self._match(receipt_sid="sid-b"))
        self.assertTrue(self._match(receipt_sid="sid-a"))

    def test_null_receipt_sid_uses_issuer_and_subject(self):
        self.assertTrue(self._match(receipt_sid=None, receipt_subject="sub-a"))
        self.assertFalse(self._match(receipt_sid=None, receipt_subject="sub-b"))
        self.assertFalse(self._match(receipt_sid=None, receipt_subject=None))

    def test_created_at_millisecond_boundary(self):
        self.assertTrue(self._match(flow_created_at_ms=1000999))
        self.assertFalse(self._match(flow_created_at_ms=1001000))
        self.assertFalse(self._match(flow_created_at_ms=1000001,
                                     receipt_received_at_ms=1000000))
        self.assertFalse(self._match(receipt_issuer="https://other.example"))

    def test_callback_form_is_equivalent_for_whole_second_iat(self):
        for created in (999999, 1000000, 1000001, 1000999, 1001000, 1001001):
            for received in (created - 1, created, created + 1000):
                values = {
                    "flow_issuer": self.ISSUER, "flow_sid": "sid-a",
                    "flow_subject": "sub-a", "flow_created_at_ms": created,
                    "receipt_issuer": self.ISSUER, "receipt_sid": "sid-a",
                    "receipt_subject": "sub-a", "receipt_issued_at_ms": 1000000,
                    "receipt_received_at_ms": received,
                }
                with self.subTest(created=created, received=received):
                    self.assertEqual(sql.receipt_matches(**values),
                                     sql.receipt_matches_callback(**values))


if __name__ == "__main__":
    unittest.main()
