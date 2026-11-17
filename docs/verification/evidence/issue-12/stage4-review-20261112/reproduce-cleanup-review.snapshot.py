"""Stage-four diagnostic injection; does not edit product or the 75-case script."""

import asyncio
from contextlib import asynccontextmanager
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import types
from unittest import mock

ROOT = Path(__file__).resolve().parent
WORKTREE = ROOT.parent / "xiaozhi/.worktrees/issue-12-mcp-contract"
SCRIPT = WORKTREE / "scripts/check-mcp-offline.py"
spec = importlib.util.spec_from_file_location("issue12_offline_review", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


async def exercise(self, caller_cancelled):
    harness = module.H
    entered, release = asyncio.Event(), asyncio.Event()
    cancellations = []

    @asynccontextmanager
    async def delayed_transport(**kwargs):
        try:
            yield object(), object()
        finally:
            entered.set()
            while not release.is_set():
                try:
                    await release.wait()
                except asyncio.CancelledError:
                    cancellations.append("cancel_observed")

    client = harness.client.ServerMCPClient({"url": "https://synthetic.invalid/sse"})
    manager = harness.manager.ServerMCPManager(types.SimpleNamespace())
    worker = None
    cleanup = None
    with mock.patch.object(harness.client, "sse_client", delayed_transport), \
            mock.patch.multiple(harness.client, _WORKER_GRACE_SECONDS=0.02,
                                _WORKER_CANCEL_WAIT_SECONDS=0.02):
        try:
            await client.initialize()
            worker = client._worker_task
            manager.clients["synthetic-review"] = client
            cleanup = asyncio.create_task(manager.cleanup_all())
            await asyncio.wait_for(entered.wait(), 1)
            if caller_cancelled:
                cleanup.cancel()
                with self.assertRaises(asyncio.CancelledError):
                    await cleanup
                await asyncio.sleep(0)
                self.assertIn("synthetic-review", manager.clients)
            else:
                await asyncio.wait_for(cleanup, 1)
            self.assertFalse(worker.done(), "stimulus must leave the transport closing")
            self.assertTrue(cancellations, "stimulus must observe worker cancellation")
            self.assertIs(client._worker_task, worker,
                          "unfinished real client worker reference must survive for retry")
            self.assertIn("synthetic-review", manager.clients,
                          "manager must not report pending worker cleanup as success")
        finally:
            release.set()
            if cleanup is not None and not cleanup.done():
                cleanup.cancel()
                await asyncio.gather(cleanup, return_exceptions=True)
            if worker is not None:
                await asyncio.wait_for(asyncio.gather(worker, return_exceptions=True), 1)
            await manager.cleanup_all()


async def test_real_client_pending_worker_reference_survives_cleanup_deadline(self):
    await exercise(self, False)


async def test_real_client_pending_worker_reference_survives_caller_cancel(self):
    await exercise(self, True)


module.FixedRegression.test_real_client_pending_worker_reference_survives_cleanup_deadline = \
    test_real_client_pending_worker_reference_survives_cleanup_deadline
module.FixedRegression.test_real_client_pending_worker_reference_survives_caller_cancel = \
    test_real_client_pending_worker_reference_survives_caller_cancel

if __name__ == "__main__":
    output = Path(sys.argv[sys.argv.index("--output") + 1])
    code = module.main()
    metadata = {
        "scope": "stage-four diagnostic injection: original 75 plus two real-client cleanup requirements",
        "wrapper_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "original_script_sha256": hashlib.sha256(SCRIPT.read_bytes()).hexdigest(),
        "product_source_modified": False,
        "network_guards": "original main Python guards active; transports synthetic; not OS sandbox",
        "ubuntu_rerun": False,
        "added_cases": [
            "test_real_client_pending_worker_reference_survives_cleanup_deadline",
            "test_real_client_pending_worker_reference_survives_caller_cancel",
        ],
    }
    (output / "review-scope.json").open("x", encoding="utf-8").write(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n")
    raise SystemExit(code)
