"""HTTP and private-file helpers for the local voice protocol checks."""
import json
import os
from pathlib import Path
import urllib.error
import urllib.request


def http_json(base, path, method="GET", body=None, token=None, headers=None):
    request_headers = {"Content-Type": "application/json", **(headers or {})}
    if token is not None:
        request_headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(
        base.rstrip("/") + path,
        data=json.dumps(body).encode() if body is not None else None,
        headers=request_headers,
        method=method,
    )
    client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        response = client.open(request, timeout=20)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        raw = response.read()
        if not raw:
            return response.status, None
        try:
            body = json.loads(raw)
        except ValueError:
            body = raw.decode("utf-8")
        return response.status, body


def api(base, path, method="GET", body=None, token=None):
    status, response = http_json(base, path, method, body, token)
    if status != 200 or not isinstance(response, dict) or response.get("code") != 0:
        code = response.get("code") if isinstance(response, dict) else None
        raise ValueError(f"{method} {path}: HTTP {status}, API code {code}")
    return response.get("data")


def private_json(path, value, exclusive=False):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    flags = os.O_WRONLY | os.O_CREAT | (os.O_EXCL if exclusive else os.O_TRUNC)
    descriptor = os.open(path, flags, 0o600)
    with os.fdopen(descriptor, "w") as file:
        os.fchmod(file.fileno(), 0o600)
        json.dump(value, file, ensure_ascii=False, indent=2)
        file.write("\n")
