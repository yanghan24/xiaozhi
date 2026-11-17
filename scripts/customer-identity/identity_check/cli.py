"""Server-only collection; offline tests inject a synthetic source."""
import argparse
import json
import os
from pathlib import Path
import re
import sys

from .errors import CheckError
from . import output
from .records import load_round
from .state import bind_baseline, browser_evidence, load_baseline, make_state
from .timeutil import parse_timestamp, validate_window


class SafeParser(argparse.ArgumentParser):
    def error(self, message):
        self.exit(2, "PARAMETER_INVALID: use --help for accepted arguments.\n")


def _parser():
    parser = SafeParser(description="Read-only identity acceptance observations. No business requests are sent.")
    parser.add_argument("scenario", choices=("login", "logout", "two-account", "idle"))
    parser.add_argument("--env", required=True, help="Explicit operator-reviewed environment JSON")
    parser.add_argument("--record")
    parser.add_argument("--record-a")
    parser.add_argument("--record-b")
    parser.add_argument("--flow", help="Safe ordinal F1..F50, never a raw identifier")
    parser.add_argument("--flow-a")
    parser.add_argument("--flow-b")
    parser.add_argument("--window-start")
    parser.add_argument("--window-end")
    parser.add_argument("--phase", choices=("baseline", "check"), default="check")
    parser.add_argument("--baseline")
    parser.add_argument("--save-state")
    parser.add_argument("--browser-evidence")
    parser.add_argument("--output")
    parser.add_argument("--format", choices=("json", "summary", "both"), default="both")
    parser.add_argument("--timeout", type=int, default=20, help="Per-command seconds, 1..60")
    return parser


def _arguments(parser, args):
    if not 1 <= args.timeout <= 60:
        parser.error("")
    dual = args.scenario == "two-account"
    if dual:
        if not args.record_a or not args.record_b or args.record or args.flow:
            parser.error("")
    elif not args.record or args.record_a or args.record_b or args.flow_a or args.flow_b:
        parser.error("")
    if args.browser_evidence and args.scenario != "idle":
        parser.error("")
    if args.phase == "baseline" and (not args.save_state or args.baseline or args.browser_evidence):
        parser.error("")
    if bool(args.window_start) != bool(args.window_end):
        parser.error("")
    if args.phase == "check" and args.scenario in ("logout", "two-account") and not args.window_start:
        parser.error("")
    if args.phase == "baseline" and args.window_start:
        parser.error("")
    targets = {label: value for label, value in (("A", args.flow_a if dual else args.flow), ("B", args.flow_b)) if value}
    for value in targets.values():
        if not re.fullmatch(r"F(?:[1-9]|[1-4][0-9]|50)", value):
            parser.error("")
    start = end = None
    if args.window_start:
        try:
            start, end = validate_window(parse_timestamp(args.window_start), parse_timestamp(args.window_end))
        except ValueError:
            parser.error("")
    return targets, start, end


def _paths(args):
    inputs = [value for value in (args.env, args.record, args.record_a, args.record_b,
                                 args.baseline, args.browser_evidence) if value]
    outputs = [value for value in (args.save_state, args.output) if value]
    normalized_inputs = {Path(value).resolve() for value in inputs}
    normalized_outputs = [Path(value).resolve() for value in outputs]
    if len(set(normalized_outputs)) != len(normalized_outputs) or set(normalized_outputs) & normalized_inputs:
        raise CheckError("OUTPUT_INPUT_COLLISION")
    if any(Path(value).exists() or Path(value).is_symlink() for value in outputs):
        raise CheckError("OUTPUT_FILE_EXISTS")


def main(argv=None, source_factory=None, stdout=None, stderr=None):
    stdout, stderr = stdout or sys.stdout, stderr or sys.stderr
    parser = _parser()
    try:
        args = parser.parse_args(argv)
        targets, start, end = _arguments(parser, args)
        _paths(args)
        if source_factory is None and (sys.platform != "linux" or os.geteuid() != 0):
            raise CheckError("RUN_ON_SERVER_WITH_APPROVED_RUNNER")
        from .envcheck import load_environment
        from .evaluate import evaluate
        from .logs import parse_capture
        from .source import ServerSource
        env = load_environment(args.env)
        names = {"A": args.record_a, "B": args.record_b} if args.scenario == "two-account" else {"A": args.record}
        records = {label: load_round(path, label, env["record_root"]) for label, path in names.items()}
        if len(records) == 2 and records["A"].handle == records["B"].handle:
            raise CheckError("RECORDS_NOT_DISTINCT")
        baseline = load_baseline(args.baseline) if args.baseline else None
        source = (source_factory or ServerSource)(env, timeout=args.timeout)
        observation = source.collect(records, start_ms=start, end_ms=end, baseline=baseline)
        observation["log"] = parse_capture(observation.pop("log_capture", {}), start, end,
                                            baseline.get("log_state") if baseline else None)
        if baseline:
            bind_baseline(baseline, args.scenario, records, observation, targets)
        preliminary = evaluate(args.scenario, observation, targets, baseline=baseline, phase=args.phase)
        selected = preliminary.get("selected_targets", {})
        evidence = None
        if args.browser_evidence and selected.get("A"):
            flow = next(row for row in observation["flows"]["A"] if row["label"] == selected["A"])
            evidence = browser_evidence(args.browser_evidence, records["A"], selected["A"], flow)
        result = evaluate(args.scenario, observation, targets, baseline=baseline,
                          browser_evidence=evidence, phase=args.phase)
        sensitive = [record.handle for record in records.values()]
        report = output.build_report(args.scenario, args.phase, observation, result)
        rendered = output.serialize(report, sensitive)
        readable = output.guard(output.summary(report), sensitive)
        if args.save_state:
            if args.phase == "baseline" and not result.get("baseline_ready"):
                raise CheckError("BASELINE_NOT_READY")
            state = make_state(args.scenario, observation, records, result.get("selected_targets", {}))
            if args.scenario == "two-account":
                state["identities_distinct"] = result.get("identities_distinct")
            private_text = json.dumps(state, ensure_ascii=False, indent=2, allow_nan=False)
            for value in sensitive:
                if value in private_text or value.encode("utf-8").hex().lower() in private_text.lower():
                    raise CheckError("OUTPUT_REDACTION_FAILED")
            output.write_private(args.save_state, private_text)
        if args.output:
            output.write_private(args.output, rendered)
        if args.format in ("json", "both"):
            stdout.write(rendered + "\n")
        if args.format in ("summary", "both"):
            (stderr if args.format == "both" else stdout).write(readable + "\n")
        return report["exit_code"]
    except CheckError as error:
        report = output.error_report(error.code)
    except KeyboardInterrupt:
        report = output.error_report("CHECK_INTERRUPTED")
    except Exception:
        report = output.error_report("CHECK_FAILED")
    stdout.write(output.serialize(report) + "\n")
    return 4
