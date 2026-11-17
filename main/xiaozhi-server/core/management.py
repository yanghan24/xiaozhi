"""Service administration boundary. Never creates a device/audio/model session."""
import asyncio
import hmac
import json
import os
import sys

PATH = "/xiaozhi/management/"


async def restart_process():
    # Keep the same PID and supervisor-owned descriptors. No second app process.
    await asyncio.sleep(1)
    os.execv(sys.executable, [sys.executable, *sys.argv])


def schedule_restart():
    task = asyncio.create_task(restart_process())
    # Keep the task alive until exec or an error; log through the event loop on failure.
    _restart_tasks.add(task)
    def finished(completed):
        _restart_tasks.discard(completed)
        if not completed.cancelled() and completed.exception() is not None:
            import logging
            logging.getLogger(__name__).error("Voice service process replacement failed")
    task.add_done_callback(finished)


_restart_tasks = set()


async def handle_management(server, websocket):
    secret = server.config.get("manager-api", {}).get("secret", "")
    authorization = websocket.request.headers.get("authorization", "")
    if (not server.config.get("read_config_from_api") or not isinstance(secret, str)
            or not secret or secret == "null" or not authorization.startswith("Bearer ")
            or not hmac.compare_digest(authorization[7:].encode(), secret.encode())):
        await websocket.close(code=1008, reason="Management authentication required")
        return
    action = ""
    try:
        raw = await asyncio.wait_for(websocket.recv(), 5)
        if not isinstance(raw, str) or len(raw) > 4096:
            raise ValueError("Invalid management request")
        message = json.loads(raw)
        if not isinstance(message, dict) or message.get("type") != "server":
            raise ValueError("Invalid management request")
        action = message.get("action")
        if action not in ("update_config", "restart"):
            raise ValueError("Unsupported management action")
        if action == "update_config":
            success = await server.update_config()
            await websocket.send(json.dumps({"type": "server", "status": "success" if success else "error",
                                             "content": {"action": action}}))
        else:
            await websocket.send(json.dumps({"type": "server", "status": "success",
                                             "content": {"action": action}}))
            schedule_restart()
    except Exception:
        await websocket.send(json.dumps({"type": "server", "status": "error", "content": {"action": action}}))
    finally:
        await websocket.close()
