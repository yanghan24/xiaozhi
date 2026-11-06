#!/usr/bin/env python3
"""Keep one marked device across an externally performed service restart.

Run inside the verified Linux voice image, where /checks contains voice_api.py
and check-voice.py. This script never restarts containers or changes defaults.
prepare creates one disposable agent/device; verify only reads/reconnects it;
cleanup deletes only the agent whose random marker and ownership match state.
The private state survives failures. Use a NEW --output path for every attempt.
"""
import argparse
import asyncio
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import sys
from urllib.parse import urlparse
import uuid


SCHEMA = "xiaozhi-device-persistence-v1"
BASELINE = "6e354edf292c3df14fd783c5dc7314a4402e3b74"
AGENT_FIELDS = (
    "id", "userId", "agentName", "asrModelId", "vadModelId", "llmModelId",
    "slmModelId", "vllmModelId", "ttsModelId", "ttsVoiceId", "ttsLanguage",
    "ttsVolume", "ttsRate", "ttsPitch", "memModelId", "intentModelId",
    "chatHistoryConf", "systemPrompt", "langCode", "language", "sort",
)


def now():
    return datetime.now(timezone.utc).isoformat()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(value):
    raw = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(raw.encode("utf-8")).hexdigest()


def private_write(path, value, exclusive=False):
    """Atomically replace state/results; initial creation refuses existing files."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    target = path if exclusive else path.with_name(path.name + ".tmp-" + uuid.uuid4().hex)
    descriptor = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            os.fchmod(handle.fileno(), 0o600)
            json.dump(value, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        if not exclusive:
            os.replace(target, path)
    finally:
        if not exclusive and target.exists():
            target.unlink()


def load_checks(directory):
    directory = Path(directory).resolve()
    sys.path.insert(0, str(directory))
    import voice_api
    spec = importlib.util.spec_from_file_location("baseline_voice_check", directory / "check-voice.py")
    require(spec is not None and spec.loader is not None, "Cannot load baseline protocol checker")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return voice_api, module


def validate_url(value, protocols, suffix=None):
    parsed = urlparse(value)
    require(parsed.scheme in protocols and bool(parsed.hostname), "Invalid endpoint URL")
    require(not parsed.username and not parsed.password and not parsed.query and not parsed.fragment,
            "Endpoint URLs must not contain credentials, query strings or fragments")
    if suffix:
        require(parsed.path.endswith(suffix), "Unexpected endpoint path: expected " + suffix)
    return value


class Check:
    def __init__(self, args, report, helpers, baseline):
        self.args, self.report, self.http, self.baseline = args, report, helpers, baseline
        self.settings = json.loads(Path(args.settings).read_text(encoding="utf-8"))
        self.secret_values = []
        for name in ("admin_token", "server_secret"):
            value = self.settings.get(name)
            require(isinstance(value, str) and bool(value), "Missing required private setting: " + name)
            self.secret_values.append(value)
        self.manager = validate_url(self.settings["manager_url"], ("http", "https"))
        self.ws_url = validate_url(self.settings["public_ws_url"], ("ws", "wss"), "/xiaozhi/v1/")
        self.token = self.settings["admin_token"]
        self.state = None

    def sanitize(self, value):
        if isinstance(value, dict):
            return {key: self.sanitize(item) for key, item in value.items()}
        if isinstance(value, list):
            return [self.sanitize(item) for item in value]
        if isinstance(value, str):
            for secret in sorted(set(self.secret_values), key=len, reverse=True):
                value = value.replace(secret, "[redacted]")
            return re.sub(r"(?i)Bearer\s+(?!\[redacted\]|\[invalid\])[^\s\"']+", "Bearer [redacted]", value)
        return value

    def pass_check(self, name, **evidence):
        self.report["checks"].append({"name": name, "status": "passed", **evidence})
        print("PASS: " + name)

    def save_state(self):
        self.state["updated_at"] = now()
        private_write(self.args.state, self.state)

    def api(self, path, method="GET", body=None, service=False):
        token = self.settings["server_secret"] if service else self.token
        return self.http.api(self.manager, path, method, body, token)

    def account(self):
        user = self.api("/user/info")
        require(isinstance(user, dict) and user.get("id") is not None and bool(user.get("username")),
                "Authenticated account identity was not returned")
        if user.get("token"):
            self.secret_values.append(user["token"])
        return {"id": str(user["id"]), "username": user["username"]}

    def load_state(self):
        self.state = json.loads(Path(self.args.state).read_text(encoding="utf-8"))
        state = self.state
        require(state.get("schema") == SCHEMA and state.get("baseline_source") == BASELINE,
                "State does not belong to this acceptance script/baseline")
        trace = state.get("run_id", "")
        require(re.fullmatch(r"[0-9a-f]{32}", trace) is not None, "Invalid state run marker")
        require(state.get("agent_name") == "persistence-" + trace and
                state.get("client_id") == "persistence-" + trace and
                state.get("device_id") == "02:" + ":".join(trace[i:i + 2] for i in range(0, 10, 2)),
                "State identity markers are inconsistent; refusing remote mutation")
        require(state.get("manager_url") == self.manager and state.get("public_ws_url") == self.ws_url,
                "Current manager/advertised WebSocket settings differ from the prepared environment")
        validate_url(state["public_ota_url"], ("http", "https"), "/xiaozhi/ota/")
        if self.args.ota_url:
            require(self.args.ota_url == state["public_ota_url"], "OTA URL differs from the prepared environment")
        self.report.update({"run_id": trace, "device_id": state["device_id"],
                            "client_id": state["client_id"], "prepared_at": state.get("prepared_at")})

    def same_account(self):
        current = self.account()
        require(current == self.state.get("account"), "Current login belongs to a different account")
        self.pass_check("same authenticated account", account_fingerprint=digest(current))
        return current

    def headers(self, alternate=False):
        return {"Device-Id": self.state["device_id"],
                "Client-Id": self.state["client_id"] + ("-other" if alternate else "")}

    def ota(self, activate=False, alternate=False):
        base = self.state["public_ota_url"][:-len("/ota/")]
        body = {} if activate else {
            "version": 1, "mac_address": self.state["device_id"], "uuid": self.state["run_id"],
            "application": {"name": "persistence-check", "version": "1.0.0"},
            "board": {"type": "protocol-check", "mac": self.state["device_id"]},
        }
        status, result = self.http.http_json(base, "/ota/activate" if activate else "/ota/",
                                           "POST", body, headers=self.headers(alternate))
        if isinstance(result, dict):
            for value in (result.get("activation", {}).get("code"), result.get("websocket", {}).get("token")):
                if isinstance(value, str) and value:
                    self.secret_values.append(value)
        return status, result

    def matching_agents(self):
        agents = self.api("/agent/list")
        require(isinstance(agents, list), "Management API did not return an agent list")
        return [item for item in agents if item.get("agentName") == self.state["agent_name"]]

    def owned_agent(self, agent_id):
        agent = self.api("/agent/" + agent_id)
        require(isinstance(agent, dict) and agent.get("id") == agent_id and
                agent.get("agentName") == self.state["agent_name"] and
                str(agent.get("userId")) == self.state["account"]["id"],
                "Agent ID, unique marker or owner changed; refusing to continue")
        return agent

    def devices(self, agent_id):
        devices = self.api("/device/bind/" + agent_id)
        require(isinstance(devices, list), "Management API did not return bound devices")
        # Agent deletion cascades to every device; never delete an unrelated one.
        require(all(item.get("macAddress", "").lower() == self.state["device_id"] for item in devices),
                "Another device is attached to the fixture agent; refusing cascade cleanup")
        require(len(devices) <= 1, "Fixture has duplicate bound device records")
        return devices

    def model_request(self):
        return {"macAddress": self.state["device_id"], "clientId": self.state["client_id"], "selectedModule": {}}

    def snapshot(self):
        agent_id = self.state["agent_id"]
        agent = self.owned_agent(agent_id)
        devices = self.devices(agent_id)
        require(len(devices) == 1, "The retained device is no longer bound to the retained agent")
        expected = self.state["requested_models"]
        require(all(agent.get(key) == value for key, value in expected.items()),
                "Retained agent does not contain the requested model configuration")
        config = self.api("/config/agent-models", "POST", self.model_request(), service=True)
        selected = config.get("selected_module")
        require(isinstance(selected, dict) and selected.get("ASR") == expected["asrModelId"] and
                config.get("chat_history_conf") == 0, "Device model configuration is not the prepared configuration")
        provider_digests = {}
        for module, identifier in selected.items():
            provider = config.get(module, {}).get(identifier)
            require(isinstance(provider, dict) and bool(provider), "Selected provider configuration is missing: " + module)
            # Hash complete provider settings; public evidence never includes keys/config values.
            provider_digests[module] = {"id": identifier, "sha256": digest(provider)}
        stable_device = {key: devices[0].get(key) for key in
                         ("id", "macAddress", "board", "appVersion", "alias", "autoUpdate", "createDateTimestamp")}
        return {"account": self.state["account"], "agent": {key: agent.get(key) for key in AGENT_FIELDS},
                "device": stable_device, "selected_module": selected, "providers": provider_digests,
                "chat_history_conf": config.get("chat_history_conf")}

    def reconnect(self):
        status, active = self.ota(activate=True)
        require(status == 200 and active == "success", "The same device is not active at the public OTA endpoint")
        self.pass_check("retained device is active through public OTA", http=status,
                        url=self.state["public_ota_url"] + "activate")
        status, response = self.ota()
        require(status == 200 and isinstance(response, dict) and not response.get("activation"),
                "Public OTA unexpectedly requires a new activation/binding")
        websocket = response.get("websocket", {})
        require(websocket.get("url") == self.state["public_ws_url"] and bool(websocket.get("token")),
                "Public OTA returned a changed URL or no device authentication token")
        self.pass_check("same public OTA returns the retained WebSocket URL", http=status,
                        websocket_url=websocket["url"], token="[redacted]")
        status, other = self.ota(alternate=True)
        require(status == 200 and isinstance(other, dict) and bool(other.get("websocket", {}).get("token")),
                "Public OTA did not issue credentials for the readiness ownership negative check")
        other_headers = {**self.headers(True), "Authorization": "Bearer " + other["websocket"]["token"]}
        # Reuses real hello, same-session ready, invalid token/other-client refusal,
        # three Ping/Pong frames, and closed-session disappearance checks.
        asyncio.run(self.baseline.check_websocket(websocket, self.headers(), other_headers, self.report))

    def prepare(self):
        require(self.args.ota_url, "prepare requires the actual public --ota-url")
        validate_url(self.args.ota_url, ("http", "https"), "/xiaozhi/ota/")
        trace = uuid.uuid4().hex
        self.state = {
            "schema": SCHEMA, "baseline_source": BASELINE, "run_id": trace,
            "agent_name": "persistence-" + trace, "client_id": "persistence-" + trace,
            "device_id": "02:" + ":".join(trace[i:i + 2] for i in range(0, 10, 2)),
            "created_at": now(), "status": "preparing", "manager_url": self.manager,
            "public_ws_url": self.ws_url, "public_ota_url": self.args.ota_url,
            "account": None, "agent_id": None, "creation_intent": False, "binding_intent": False,
            "pending_activation_cache": False,
            "requested_models": {
                "asrModelId": self.settings["asr_model"], "vadModelId": "VAD_SileroVAD",
                "llmModelId": "LLM_ChatGLMLLM", "ttsModelId": self.settings.get("tts_model", "TTS_EdgeTTS"),
                "intentModelId": "Intent_nointent", "memModelId": "Memory_nomem", "chatHistoryConf": 0,
            },
        }
        private_write(self.args.state, self.state, exclusive=True)
        self.report.update({"run_id": trace, "device_id": self.state["device_id"], "client_id": self.state["client_id"]})
        self.state["account"] = self.account()
        self.save_state()
        require(not self.matching_agents(), "Unexpected pre-existing agent marker; refusing creation")
        status, response = self.http.http_json(self.manager, "/config/agent-models", "POST", self.model_request(),
                                               self.settings["server_secret"])
        require(status == 200 and isinstance(response, dict) and response.get("code") == 10041,
                "Generated device identity is not fresh; refusing to use it")
        self.pass_check("fixture identity was absent before creation", code=10041)
        # Save intent before POST: cleanup can find an exact marker after a lost response.
        self.state["creation_intent"] = True
        self.save_state()
        agent_id = self.api("/agent", "POST", {"agentName": self.state["agent_name"]})
        require(isinstance(agent_id, str) and bool(agent_id), "Agent creation did not return an ID")
        self.state["agent_id"] = agent_id
        self.save_state()
        self.owned_agent(agent_id)
        self.api("/agent/" + agent_id, "PUT", self.state["requested_models"])
        self.pass_check("only the marked agent was created and configured", agent_id=agent_id)
        status, active = self.ota(activate=True)
        require(status == 202, "Fresh fixture unexpectedly appears bound at public OTA")
        self.state["pending_activation_cache"] = True
        self.save_state()
        status, response = self.ota()
        code = response.get("activation", {}).get("code") if isinstance(response, dict) else None
        require(status == 200 and isinstance(code, str) and len(code) == 6 and code.isdigit(),
                "Public OTA did not return a six-digit activation code")
        self.state["binding_intent"] = True
        self.save_state()
        self.api("/device/bind/" + agent_id + "/" + code, "POST")
        self.state["pending_activation_cache"] = False
        self.save_state()
        snapshot = self.snapshot()
        self.pass_check("retained binding and model configuration read back", agent_id=agent_id,
                        account_fingerprint=digest(self.state["account"]), snapshot_sha256=digest(snapshot),
                        selected_module=snapshot["selected_module"])
        self.reconnect()
        self.state["baseline_snapshot"] = snapshot
        self.state["prepared_at"] = now()
        self.state["status"] = "prepared"
        self.save_state()
        self.report["prepared_at"] = self.state["prepared_at"]
        self.report["next_step"] = "Record Docker container start times, restart only the isolated project, then run verify with this state."

    def verify(self):
        self.load_state()
        require(self.state.get("status") in ("prepared", "verified") and self.state.get("baseline_snapshot"),
                "Only a successfully prepared, uncleared fixture can be verified")
        self.same_account()
        snapshot = self.snapshot()
        expected = self.state["baseline_snapshot"]
        differences = [key for key in expected if snapshot.get(key) != expected[key]]
        self.report["snapshot_comparison"] = {"before_sha256": digest(expected), "after_sha256": digest(snapshot),
                                              "changed_sections": differences}
        require(not differences, "Retained binding/configuration changed in: " + ", ".join(differences))
        self.pass_check("same device, account, agent and model configuration survived", agent_id=self.state["agent_id"],
                        selected_module=snapshot["selected_module"], snapshot_sha256=digest(snapshot))
        self.reconnect()
        self.state["status"] = "verified"
        self.state["verified_at"] = now()
        self.save_state()
        self.report["restart_evidence"] = "External Docker before/after evidence is required; this script does not perform or independently prove a container restart."

    def cleanup(self):
        self.load_state()
        if not self.state.get("creation_intent"):
            self.state["status"] = "cleaned"
            self.save_state()
            self.pass_check("no remote object creation was attempted")
            return
        self.same_account()
        matching = self.matching_agents()
        require(len(matching) <= 1, "Multiple agents share the fixture marker; refusing ambiguous cleanup")
        if matching:
            agent_id = matching[0].get("id")
            require(isinstance(agent_id, str) and bool(agent_id), "Marked agent has no ID")
            if self.state.get("agent_id"):
                require(agent_id == self.state["agent_id"], "Marker now refers to a different agent; refusing deletion")
            self.owned_agent(agent_id)
            self.devices(agent_id)
            self.state["agent_id"] = agent_id
            self.state["cleanup_intent"] = True
            self.save_state()
            self.api("/agent/" + agent_id, "DELETE")
            require(not self.matching_agents(), "Fixture agent deletion did not persist")
            self.pass_check("only the marked owned agent and its fixture device were deleted", agent_id=agent_id)
        else:
            # A previous DELETE may have succeeded even if its HTTP response was lost.
            if self.state.get("agent_id"):
                agents = self.api("/agent/list")
                require(all(item.get("id") != self.state["agent_id"] for item in agents),
                        "Fixture agent still exists with a changed marker; refusing cleanup")
            self.pass_check("marked agent is already absent")
        status, _ = self.ota(activate=True)
        require(status == 202, "Fixture device remains bound after cleanup; no unrelated object will be removed")
        self.pass_check("fixture device is no longer bound", public_activation_http=status)
        if self.state.get("pending_activation_cache"):
            self.report["temporary_activation_cache"] = "An unbound activation request may remain in Redis until the upstream 24-hour TTL; no cache administration or unrelated cleanup is performed."
        self.state["status"] = "cleaned"
        self.state["cleaned_at"] = now()
        self.save_state()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("prepare", "verify", "cleanup"))
    parser.add_argument("--settings", default="/app/data/check.json")
    parser.add_argument("--state", default="/app/data/persistence-state.json")
    parser.add_argument("--output", required=True, help="New sanitized JSON file; existing evidence is never overwritten")
    parser.add_argument("--ota-url", help="Actual public device OTA URL; required for prepare, then fixed in state")
    parser.add_argument("--checks-dir", default="/checks")
    args = parser.parse_args()
    report = {"scope": "same_device_persistence", "phase": args.phase, "status": "failed", "started_at": now(),
              "baseline_source": BASELINE, "checks": [], "audio_inference": "not_run", "hardware_playback": "not_run",
              "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    checker = None
    lock = Path(args.state).with_name(Path(args.state).name + ".lock")
    locked = False
    output_created = False
    try:
        require(os.name == "posix", "Run this candidate in the Linux voice image; private files require POSIX permissions")
        paths = [Path(item).resolve() for item in (args.settings, args.state, args.output, str(lock), __file__)]
        require(len(set(paths)) == len(paths), "Settings, state, output, lock and script paths must be different")
        private_write(args.output, report, exclusive=True)
        output_created = True
        private_write(lock, {"pid": os.getpid(), "phase": args.phase, "at": now()}, exclusive=True)
        locked = True
        if args.phase == "prepare":
            require(not Path(args.state).exists(), "State already exists; use verify/cleanup or choose a new state file")
        helpers, baseline = load_checks(args.checks_dir)
        checker = Check(args, report, helpers, baseline)
        getattr(checker, args.phase)()
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        message = str(error) or "Interrupted"
        if checker is not None:
            message = checker.sanitize(message)
        report["error"] = message
        report["error_type"] = type(error).__name__
        print("FAIL: " + message, file=sys.stderr)
    finally:
        report["finished_at"] = now()
        if checker is not None:
            report = checker.sanitize(report)
        if output_created:
            try:
                private_write(args.output, report)
            except Exception as error:
                report["status"] = "failed"
                print("FAIL: unable to finalize evidence: " + type(error).__name__, file=sys.stderr)
        if locked:
            lock.unlink(missing_ok=True)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
