import json
import os
import tempfile
import unittest
from unittest import mock

from identity_check import envcheck, sql
from identity_check.errors import CheckError


CONFIG = """\
http {
  upstream identity_up { server identity1:8080; }
  server {
    # BEGIN SYNTHETIC TRUSTED ROUTES
    location = /xiaozhi/customer/oidc/backchannel-logout {
      access_log /var/log/nginx/notify.log notify;
      proxy_pass http://identity1:8080;
    }
    # END SYNTHETIC TRUSTED ROUTES
  }
}
"""


def _schema_rows(version="8.0.36"):
    rows = [{"kind": "version", "version": version}]
    types = {"string": "varchar", "bigint": "bigint", "integer": "bigint",
             "text": "text", "boolean": "tinyint"}
    for table in sorted(sql.SCHEMA_REQUIRED):
        for column, family in sql.SCHEMA_REQUIRED[table]:
            rows.append({"kind": "column", "table": table, "name": column,
                         "type": types[family]})
    return rows


def _schema_output(rows):
    return ("\n".join(json.dumps(row) for row in rows) + "\n").encode("utf-8")


class Result(object):
    def __init__(self, returncode=0, stdout=b"", stderr=b"", overflow=False):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
        self.overflow = overflow


class FakeRunner(object):
    def __init__(self):
        self.calls = []
        self.version_returncode = 0
        self.running = {"db1": True, "identity1": True, "entry1": True}
        self.networks = {
            "db1": {"sqlshared": {"Aliases": ["db1", "mysql"]}},
            "identity1": {
                "sqlshared": {"Aliases": ["identity1"]},
                "entrynet": {"Aliases": ["identity1"]},
            },
            "entry1": {"entrynet": {"Aliases": ["entry1"]}},
        }
        self.mounts = [{"Type": "bind", "Source": "/srv/entry",
                        "Destination": "/etc/nginx"}]
        self.exposed_ports = {"8080/tcp": {}}
        self.peers = ["db1", "identity1", "entry1"]
        self.jdbc = b"jdbc:mysql://mysql:3306/dbname?useUnicode=true"
        self.schema_rows = _schema_rows()
        self.mysql_returncode = 0
        self.mysql_stderr = b""
        self.mysql_stdout = None
        self.mysql_calls = []

    def run(self, argv, input_text=None, timeout=None, max_stdout=None):
        self.calls.append(list(argv))
        if argv[:2] == ["docker", "version"]:
            if self.version_returncode:
                return Result(self.version_returncode, b"")
            return Result(0, b"8.0.36\n")
        if argv[:2] == ["docker", "inspect"]:
            fmt, name = argv[3], argv[4]
            if fmt == "{{json .State.Running}}":
                return Result(0, json.dumps(self.running.get(name, False)).encode())
            if fmt == "{{json .NetworkSettings.Networks}}":
                return Result(0, json.dumps(self.networks.get(name, {})).encode())
            if fmt == "{{json .Mounts}}":
                return Result(0, json.dumps(self.mounts).encode())
            if fmt == "{{json .Config.ExposedPorts}}":
                return Result(0, json.dumps(self.exposed_ports).encode())
            if fmt == "{{.Id}}":
                return Result(0, ("a" * 64 + "\n").encode())
            return Result(1, b"")
        if argv[:2] == ["docker", "ps"]:
            return Result(0, ("\n".join(self.peers) + "\n").encode())
        if argv[:2] == ["docker", "exec"] and argv[2] == "identity1":
            if argv[3:] == ["printenv", "SPRING_DATASOURCE_DRUID_URL"]:
                return Result(0, self.jdbc + b"\n")
            return Result(1, b"")
        return Result(1, b"")

    def mysql(self, script, container, timeout=None, max_stdout=None):
        self.mysql_calls.append((script, container))
        if self.mysql_stdout is not None:
            return Result(self.mysql_returncode, self.mysql_stdout, self.mysql_stderr)
        return Result(self.mysql_returncode, _schema_output(self.schema_rows),
                      self.mysql_stderr)


def _env():
    return {
        "schema": envcheck.ENV_SCHEMA,
        "issuer": "https://issuer.example/oidc",
        "record_root": "/home/test/records",
        "db": {"container": "db1", "database": "dbname",
               "network_alias": "mysql"},
        "identity_container": "identity1",
        "entry": {"container": "entry1", "notify_log": "/var/log/nginx/notify.log",
                  "config_file": "/srv/entry/nginx.conf",
                  "marker": "SYNTHETIC TRUSTED ROUTES"},
    }


class EnvironmentFileTest(unittest.TestCase):
    def test_load_normalizes_and_drops_extra_keys(self):
        data = _env()
        data["extra"] = "ignored"
        data["db"]["extra"] = "ignored"
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "env.json")
            with open(path, "w", encoding="utf-8") as stream:
                json.dump(data, stream)
            normalized = envcheck.load_environment(path)
        self.assertEqual(set(normalized), {"schema", "issuer", "record_root", "db",
                                           "identity_container", "entry"})
        self.assertEqual(set(normalized["db"]), {"container", "database",
                                                 "network_alias"})
        self.assertEqual(set(normalized["entry"]), {"container", "notify_log",
                                                    "config_file", "marker"})
        self.assertNotIn("extra", normalized)

    def test_missing_and_invalid_files_use_fixed_codes(self):
        with self.assertRaises(CheckError) as caught:
            envcheck.load_environment("/nonexistent/env.json")
        self.assertEqual(caught.exception.code, "ENV_FILE_MISSING")
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "env.json")
            with open(path, "w", encoding="utf-8") as stream:
                stream.write("{not json")
            with self.assertRaises(CheckError) as caught:
                envcheck.load_environment(path)
            self.assertEqual(caught.exception.code, "ENV_FILE_INVALID")
            with open(path, "w", encoding="utf-8") as stream:
                stream.write('{"schema": "xiaozhi-identity-env-v1",'
                             '"schema": "xiaozhi-identity-env-v1"}')
            with self.assertRaises(CheckError) as caught:
                envcheck.load_environment(path)
            self.assertEqual(caught.exception.code, "ENV_FILE_INVALID")

    def test_invalid_fields_are_rejected_without_echoing_values(self):
        cases = []
        bad = _env()
        bad["schema"] = "other"
        cases.append(bad)
        for field, value in (("issuer", "http://issuer.example"),
                             ("record_root", "relative/path"),
                             ("identity_container", "bad/name")):
            item = _env()
            item[field] = value
            cases.append(item)
        for section, field, value in (
                ("db", "container", "bad name"), ("db", "database", "db-name"),
                ("db", "network_alias", "bad alias"),
                ("entry", "container", ""),
                ("entry", "notify_log", "relative.log"),
                ("entry", "notify_log", "/var/log/../secret"),
                ("entry", "config_file", "/srv/../secret")):
            item = _env()
            item[section][field] = value
            cases.append(item)
        duplicate = _env()
        duplicate["identity_container"] = duplicate["db"]["container"]
        cases.append(duplicate)
        for index, item in enumerate(cases):
            with self.subTest(case=index):
                with self.assertRaises(CheckError):
                    envcheck.validate_environment(item)
        with self.assertRaises(CheckError) as caught:
            envcheck.validate_environment({"schema": "other"})
        self.assertEqual(caught.exception.code, "ENV_SCHEMA_UNSUPPORTED")


class CheckEnvironmentTest(unittest.TestCase):
    def _check(self, runner):
        with mock.patch.object(envcheck, "_read_host_text", return_value=CONFIG):
            return envcheck.check_environment(_env(), runner)

    def test_all_checks_ok_without_whole_container_environment(self):
        runner = FakeRunner()
        summary = self._check(runner)
        self.assertEqual(summary, {"containers": "ok", "db_target": "ok",
                                   "proxy_target": "ok", "schema": "ok"})
        self.assertEqual(len(runner.mysql_calls), 1)
        self.assertIn("information_schema.COLUMNS", runner.mysql_calls[0][0])
        formats = [call[3] for call in runner.calls
                   if call[:2] == ["docker", "inspect"]]
        self.assertNotIn("{{json .}}", formats)
        self.assertNotIn("{{json .Config}}", formats)
        printenv = [call for call in runner.calls
                    if call[:3] == ["docker", "exec", "identity1"]]
        self.assertTrue(printenv)
        self.assertEqual(printenv[0][3:], ["printenv", "SPRING_DATASOURCE_DRUID_URL"])

    def test_docker_unavailable_or_container_stopped(self):
        runner = FakeRunner()
        runner.version_returncode = 1
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "DOCKER_UNAVAILABLE")
        runner = FakeRunner()
        runner.running["entry1"] = False
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "CONTAINER_NOT_RUNNING")

    def test_database_alias_missing_and_ambiguous(self):
        runner = FakeRunner()
        runner.networks["db1"] = {"sqlshared": {"Aliases": ["db1"]}}
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "DB_TARGET_MISMATCH")

        runner = FakeRunner()
        runner.networks["db1"] = {
            "sqlshared": {"Aliases": ["db1", "mysql"]},
            "sqlshared2": {"Aliases": ["db1", "mysql"]},
        }
        runner.networks["identity1"]["sqlshared2"] = {"Aliases": ["identity1"]}
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "DB_ALIAS_AMBIGUOUS")

        runner = FakeRunner()
        runner.peers.append("peer1")
        runner.networks["peer1"] = {"sqlshared": {"Aliases": ["peer1", "mysql"]}}
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "DB_ALIAS_AMBIGUOUS")

    def test_db_alias_on_another_identity_network_is_ambiguous(self):
        # Review P2: the identity container may resolve "mysql" on any network
        # it joins, not only the one shared with the configured database.
        runner = FakeRunner()
        runner.networks["identity1"]["othernet"] = {"Aliases": ["identity1"]}
        runner.peers.append("otherdb")
        runner.networks["otherdb"] = {"othernet": {"Aliases": ["otherdb", "mysql"]}}
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "DB_ALIAS_AMBIGUOUS")

        runner = FakeRunner()
        runner.networks["identity1"]["othernet"] = {"Aliases": ["identity1"]}
        runner.peers.append("mysql")
        runner.networks["mysql"] = {"othernet": {"Aliases": ["mysql"]}}
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "DB_ALIAS_AMBIGUOUS")

    def test_identity_own_db_alias_on_any_network_is_ambiguous(self):
        for network in ("sqlshared", "othernet"):
            with self.subTest(network=network):
                runner = FakeRunner()
                runner.networks["identity1"][network] = {
                    "Aliases": ["identity1", "mysql"]}
                with self.assertRaises(CheckError) as caught:
                    self._check(runner)
                self.assertEqual(caught.exception.code, "DB_ALIAS_AMBIGUOUS")

    def test_identity_container_name_cannot_be_the_db_alias(self):
        runner = FakeRunner()
        env = _env()
        env["identity_container"] = "mysql"
        runner.running["mysql"] = True
        runner.networks["mysql"] = runner.networks.pop("identity1")
        with mock.patch.object(envcheck, "_read_host_text", return_value=CONFIG):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(env, runner)
        self.assertEqual(caught.exception.code, "DB_ALIAS_AMBIGUOUS")

    def test_identity_second_network_without_db_alias_is_valid(self):
        runner = FakeRunner()
        runner.networks["identity1"]["othernet"] = {"Aliases": ["identity1"]}
        self.assertEqual(self._check(runner)["db_target"], "ok")

    def test_db_alias_on_a_network_the_identity_cannot_see_is_unique(self):
        runner = FakeRunner()
        runner.peers.append("otherdb")
        runner.networks["otherdb"] = {"unrelated": {"Aliases": ["otherdb", "mysql"]}}
        self.assertEqual(self._check(runner)["db_target"], "ok")

    def test_peer_list_is_capped(self):
        runner = FakeRunner()
        runner.peers = ["peer%d" % index for index in range(101)]
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "ENV_PEER_LIMIT")

    def test_jdbc_host_and_database_must_match(self):
        runner = FakeRunner()
        runner.jdbc = b"jdbc:mysql://db1:3306/dbname"
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "DB_TARGET_MISMATCH")
        runner = FakeRunner()
        runner.jdbc = b"jdbc:mysql://mysql:3306/other"
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "DB_TARGET_MISMATCH")
        runner = FakeRunner()
        runner.jdbc = b"jdbc:mysql://mysql:3307/dbname"
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "DB_TARGET_MISMATCH")
        runner = FakeRunner()
        runner.jdbc = b"jdbc:mysql://mysql/dbname"
        self.assertEqual(self._check(runner)["db_target"], "ok")

    def test_identity_self_alias_mysql_is_ambiguous(self):
        runner = FakeRunner()
        runner.networks["identity1"]["sqlshared"]["Aliases"].append("mysql")
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "DB_ALIAS_AMBIGUOUS")

    def test_proxy_target_mismatch_and_ambiguity(self):
        runner = FakeRunner()
        config = CONFIG.replace("proxy_pass http://identity1:8080;",
                                "proxy_pass http://other:8080;")
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), runner)
        self.assertEqual(caught.exception.code, "PROXY_TARGET_MISMATCH")

        runner = FakeRunner()
        config = CONFIG.replace(
            "proxy_pass http://identity1:8080;",
            "proxy_pass http://identity1:8080;\n      proxy_pass http://identity1:8080;")
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), runner)
        self.assertEqual(caught.exception.code, "PROXY_TARGET_AMBIGUOUS")

    def test_explicit_upstream_resolution(self):
        runner = FakeRunner()
        config = CONFIG.replace("proxy_pass http://identity1:8080;",
                                "proxy_pass http://identity_up;")
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            summary = envcheck.check_environment(_env(), runner)
        self.assertEqual(summary["proxy_target"], "ok")

        # nginx refuses "upstream ... may not have port"; never accept it.
        with_port = CONFIG.replace("proxy_pass http://identity1:8080;",
                                   "proxy_pass http://identity_up:8080;")
        with mock.patch.object(envcheck, "_read_host_text", return_value=with_port):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), FakeRunner())
        self.assertEqual(caught.exception.code, "PROXY_TARGET_MISMATCH")

        config = config.replace(
            "upstream identity_up { server identity1:8080; }",
            "upstream identity_up { server identity1:8080; server other:8080; }")
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), FakeRunner())
        self.assertEqual(caught.exception.code, "PROXY_TARGET_AMBIGUOUS")

    def test_marker_block_with_login_and_notify_chooses_exact_notify(self):
        login = ("    location = /xiaozhi/customer/oidc/callback {\n"
                 "      proxy_pass http://identity1:8080;\n"
                 "    }\n")
        config = CONFIG.replace("    # BEGIN SYNTHETIC TRUSTED ROUTES\n",
                                "    # BEGIN SYNTHETIC TRUSTED ROUTES\n" + login)
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            self.assertTrue(envcheck.entry_log_configured(_env(), FakeRunner()))
            self.assertEqual(envcheck.check_environment(_env(), FakeRunner()),
                             {"containers": "ok", "db_target": "ok",
                              "proxy_target": "ok", "schema": "ok"})

    def test_marker_fallback_requires_the_enclosing_notify_location(self):
        # Review P2: a marker whose block only holds a proxy_pass must sit
        # inside the notification location; another route must not pass.
        login = CONFIG.replace("/xiaozhi/customer/oidc/backchannel-logout",
                               "/xiaozhi/customer/oidc/callback")
        nested_login = (
            "http {\n  server {\n"
            "    location = /xiaozhi/customer/oidc/callback {\n"
            "      # BEGIN SYNTHETIC TRUSTED ROUTES\n"
            "      access_log /var/log/nginx/notify.log notify;\n"
            "      proxy_pass http://identity1:8080;\n"
            "      # END SYNTHETIC TRUSTED ROUTES\n"
            "    }\n  }\n}\n")
        for name, config in (("login-route", login), ("nested-login", nested_login)):
            with self.subTest(case=name):
                with mock.patch.object(envcheck, "_read_host_text", return_value=config):
                    with self.assertRaises(CheckError) as caught:
                        envcheck.check_environment(_env(), FakeRunner())
                self.assertEqual(caught.exception.code, "PROXY_TARGET_MISMATCH")

    def test_marker_nested_inside_notify_location_is_accepted(self):
        nested = (
            "http {\n  server {\n"
            "    location = /xiaozhi/customer/oidc/backchannel-logout {\n"
            "      # BEGIN SYNTHETIC TRUSTED ROUTES\n"
            "      access_log /var/log/nginx/notify.log notify;\n"
            "      proxy_pass http://identity1:8080;\n"
            "      # END SYNTHETIC TRUSTED ROUTES\n"
            "    }\n  }\n}\n")
        with mock.patch.object(envcheck, "_read_host_text", return_value=nested):
            self.assertTrue(envcheck.entry_log_configured(_env(), FakeRunner()))
            self.assertEqual(envcheck.check_environment(_env(), FakeRunner())["proxy_target"],
                             "ok")

    def test_named_upstream_without_port_uses_member_port(self):
        # Review P2: proxy_pass to a named upstream carries no port; the
        # member's port is what must be declared by the identity container.
        config = CONFIG.replace("proxy_pass http://identity1:8080;",
                                "proxy_pass http://identity_up;")
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            self.assertEqual(envcheck.check_environment(_env(), FakeRunner())["proxy_target"],
                             "ok")
        wrong_member = config.replace("server identity1:8080;", "server identity1:9090;")
        with mock.patch.object(envcheck, "_read_host_text", return_value=wrong_member):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), FakeRunner())
        self.assertEqual(caught.exception.code, "PROXY_TARGET_MISMATCH")

    def test_duplicate_proxy_alias_on_entry_network_is_ambiguous(self):
        # Review P2: Docker DNS may resolve a duplicated alias to another
        # container, so the proxy host must be unique on entry networks.
        config = CONFIG.replace("proxy_pass http://identity1:8080;",
                                "proxy_pass http://shared_identity:8080;")
        runner = FakeRunner()
        runner.networks["identity1"]["entrynet"]["Aliases"].append("shared_identity")
        runner.peers.append("peer1")
        runner.networks["peer1"] = {"entrynet": {"Aliases": ["peer1", "shared_identity"]}}
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), runner)
        self.assertEqual(caught.exception.code, "PROXY_TARGET_AMBIGUOUS")

        upstream = CONFIG.replace("upstream identity_up { server identity1:8080; }",
                                  "upstream identity_up { server shared_identity:8080; }")
        upstream = upstream.replace("proxy_pass http://identity1:8080;",
                                    "proxy_pass http://identity_up;")
        with mock.patch.object(envcheck, "_read_host_text", return_value=upstream):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), runner)
        self.assertEqual(caught.exception.code, "PROXY_TARGET_AMBIGUOUS")

    def test_alias_shared_only_on_a_network_the_entry_cannot_see_is_unique(self):
        config = CONFIG.replace("proxy_pass http://identity1:8080;",
                                "proxy_pass http://shared_identity:8080;")
        runner = FakeRunner()
        runner.networks["identity1"]["entrynet"]["Aliases"].append("shared_identity")
        runner.peers.append("peer1")
        runner.networks["peer1"] = {"sqlshared": {"Aliases": ["peer1", "shared_identity"]}}
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            self.assertEqual(envcheck.check_environment(_env(), runner)["proxy_target"], "ok")

    def test_notify_location_must_be_an_exact_match(self):
        # Review P2: a prefix location can lose to a regex location in nginx,
        # so only "location = <notify path>" proves where notifications go.
        regex = ('    location ~ ^/xiaozhi/customer/oidc/ {\n'
                 '      proxy_pass http://other:8080;\n'
                 '    }\n')
        prefix = CONFIG.replace("location = /xiaozhi/customer/oidc/backchannel-logout",
                                "location /xiaozhi/customer/oidc/backchannel-logout")
        for name, config in (
            ("prefix", prefix),
            ("prefix-with-regex", prefix.replace("    # END SYNTHETIC", regex + "    # END SYNTHETIC")),
            ("preferential-prefix", CONFIG.replace(
                "location = /xiaozhi/customer/oidc/backchannel-logout",
                "location ^~ /xiaozhi/customer/oidc/backchannel-logout")),
        ):
            for marker in ("SYNTHETIC TRUSTED ROUTES", None):
                with self.subTest(case=name, marker=marker):
                    env = _env()
                    env["entry"]["marker"] = marker
                    with mock.patch.object(envcheck, "_read_host_text", return_value=config):
                        with self.assertRaises(CheckError) as caught:
                            envcheck.check_environment(env, FakeRunner())
                    self.assertEqual(caught.exception.code, "PROXY_TARGET_MISMATCH")

    def test_quoted_exact_notify_location_is_accepted(self):
        config = CONFIG.replace("location = /xiaozhi/customer/oidc/backchannel-logout",
                                'location = "/xiaozhi/customer/oidc/backchannel-logout"')
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            self.assertEqual(envcheck.check_environment(_env(), FakeRunner())["proxy_target"],
                             "ok")

    def test_log_not_configured_still_validates_proxy_environment(self):
        runner = FakeRunner()
        config = CONFIG.replace("      access_log /var/log/nginx/notify.log notify;\n",
                                "")
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            self.assertFalse(envcheck.entry_log_configured(_env(), runner))
            self.assertEqual(envcheck.check_environment(_env(), FakeRunner())["proxy_target"],
                             "ok")

    def test_proxy_port_must_be_declared_by_identity_container(self):
        runner = FakeRunner()
        config = CONFIG.replace("proxy_pass http://identity1:8080;",
                                "proxy_pass http://identity1:9090;")
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), runner)
        self.assertEqual(caught.exception.code, "PROXY_TARGET_MISMATCH")
        runner = FakeRunner()
        runner.exposed_ports = {}
        with mock.patch.object(envcheck, "_read_host_text", return_value=CONFIG):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), runner)
        self.assertEqual(caught.exception.code, "PROXY_TARGET_MISMATCH")

    def test_entry_must_share_a_network_with_identity(self):
        runner = FakeRunner()
        del runner.networks["identity1"]["entrynet"]
        with mock.patch.object(envcheck, "_read_host_text", return_value=CONFIG):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), runner)
        self.assertEqual(caught.exception.code, "PROXY_TARGET_MISMATCH")

    def test_network_alias_is_an_allowed_proxy_target(self):
        runner = FakeRunner()
        runner.networks["identity1"]["entrynet"]["Aliases"].append("identity-alias")
        config = CONFIG.replace("proxy_pass http://identity1:8080;",
                                "proxy_pass http://identity-alias:8080;")
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            self.assertEqual(envcheck.check_environment(_env(), runner)["proxy_target"],
                             "ok")

    def test_marker_block_must_be_unique(self):
        runner = FakeRunner()
        config = CONFIG.replace("# BEGIN SYNTHETIC TRUSTED ROUTES",
                                "# BEGIN OTHER")
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), runner)
        self.assertEqual(caught.exception.code, "PROXY_TARGET_AMBIGUOUS")
        doubled = CONFIG + CONFIG
        with mock.patch.object(envcheck, "_read_host_text", return_value=doubled):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), runner)
        self.assertEqual(caught.exception.code, "PROXY_TARGET_AMBIGUOUS")

    def test_unmarked_config_uses_the_single_location_with_the_log(self):
        runner = FakeRunner()
        config = CONFIG.replace("    # BEGIN SYNTHETIC TRUSTED ROUTES\n", "")
        config = config.replace("    # END SYNTHETIC TRUSTED ROUTES\n", "")
        env = _env()
        env["entry"]["marker"] = None
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            self.assertTrue(envcheck.entry_log_configured(env, runner))
            self.assertEqual(envcheck.check_environment(env, FakeRunner())["proxy_target"],
                             "ok")

    def test_unmarked_config_with_two_matching_locations_is_ambiguous(self):
        block = CONFIG[CONFIG.index("    location"):CONFIG.index("    # END")]
        config = "http {\n  server {\n" + block + block + "  }\n}\n"
        env = _env()
        env["entry"]["marker"] = None
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(env, FakeRunner())
        self.assertEqual(caught.exception.code, "PROXY_TARGET_AMBIGUOUS")

    def test_config_must_be_mounted_and_readable(self):
        runner = FakeRunner()
        runner.mounts = [{"Type": "bind", "Source": "/opt/other",
                          "Destination": "/etc/nginx"}]
        with mock.patch.object(envcheck, "_read_host_text", return_value=CONFIG):
            with self.assertRaises(CheckError) as caught:
                envcheck.check_environment(_env(), runner)
        self.assertEqual(caught.exception.code, "PROXY_CONFIG_NOT_MOUNTED")
        runner = FakeRunner()
        with self.assertRaises(CheckError) as caught:
            envcheck.check_environment(_env(), runner)
        self.assertEqual(caught.exception.code, "PROXY_CONFIG_MISSING")

    def test_access_log_presence_is_reported_separately(self):
        runner = FakeRunner()
        config = CONFIG.replace("/var/log/nginx/notify.log",
                                "/var/log/nginx/other.log")
        with mock.patch.object(envcheck, "_read_host_text", return_value=config):
            self.assertFalse(envcheck.entry_log_configured(_env(), runner))
            self.assertEqual(envcheck.check_environment(_env(), runner)["proxy_target"],
                             "ok")
        with mock.patch.object(envcheck, "_read_host_text", return_value=CONFIG):
            self.assertTrue(envcheck.entry_log_configured(_env(), FakeRunner()))

    def test_schema_version_and_column_mismatch(self):
        runner = FakeRunner()
        runner.schema_rows = _schema_rows("5.7.44")
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "DB_VERSION_UNSUPPORTED")

        runner = FakeRunner()
        runner.schema_rows = [row for row in _schema_rows()
                              if not (row.get("table") == "customer_identity_flow"
                                      and row.get("name") == "creation_order")]
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "SCHEMA_MISMATCH")

    def test_mysql_error_is_reduced_to_fixed_code(self):
        runner = FakeRunner()
        runner.mysql_returncode = 1
        runner.mysql_stderr = b"ERROR 1045 (28000): synthetic secret handle"
        with self.assertRaises(CheckError) as caught:
            self._check(runner)
        self.assertEqual(caught.exception.code, "MYSQL_AUTH_FAILED")
        self.assertNotIn("synthetic", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
