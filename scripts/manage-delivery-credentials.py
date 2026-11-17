"""Issue, list or revoke delivery-only workstation credentials using a normal admin login."""
import argparse
import json
import os
from pathlib import Path
import stat
import sys
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def private_json(path):
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or (os.name != "nt" and info.st_mode & 0o077):
        raise ValueError("The administrator token must be a private regular file (0600 on Unix).")
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    token = value.get("token")
    if not isinstance(token, str) or not token or len(token) > 4000 or any(ord(c) < 33 or ord(c) > 126 for c in token):
        raise ValueError("Administrator token file must contain a valid token field.")
    return token


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin", required=True, help="Trusted public HTTPS origin")
    parser.add_argument("--admin-token", type=Path, required=True)
    sub = parser.add_subparsers(dest="action", required=True)
    issue = sub.add_parser("issue")
    issue.add_argument("--name", required=True)
    issue.add_argument("--output", type=Path, required=True, help="New private file; never overwritten")
    sub.add_parser("list")
    revoke = sub.add_parser("revoke")
    revoke.add_argument("--id", required=True)
    args = parser.parse_args()
    reserved = None
    try:
        target = urlsplit(args.origin)
        if target.scheme != "https" or not target.hostname or target.username is not None or target.path or target.query or target.fragment:
            raise ValueError("Use a trusted HTTPS origin with no path, credentials, query or fragment.")
        token = private_json(args.admin_token)
        path = "/xiaozhi/device-delivery/v1/credentials"
        body = None
        if args.action == "issue":
            # Reserve the destination before issuing a non-recoverable secret.
            reserved = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            body = {"version": 1, "name": args.name}
        elif args.action == "revoke":
            if len(args.id) != 32 or any(c not in "0123456789abcdef" for c in args.id):
                raise ValueError("Invalid credential ID.")
            path += "/" + args.id + "/revoke"
            body = {"version": 1}
        request = Request(args.origin + path, method="GET" if body is None else "POST",
                          headers={"Authorization": "Bearer " + token, "Content-Type": "application/json"},
                          data=None if body is None else json.dumps(body).encode())
        with build_opener(NoRedirect()).open(request, timeout=30) as response:
            value = json.load(response)
        if value.get("version") != 1 or value.get("code") != "OK":
            raise ValueError("Server rejected the credential operation.")
        data = value["data"]
        if args.action == "issue":
            secret = data.pop("tool_token")
            package = {"tool_token": secret, "credential_id": data["credential_id"],
                       "expires_at_ms": None, "scope": "device_delivery", "origin": args.origin}
            with os.fdopen(reserved, "w", encoding="utf-8") as output:
                reserved = None
                json.dump(package, output, indent=2)
                output.write("\n")
                output.flush()
                os.fsync(output.fileno())
            data["saved_to"] = str(args.output)
        print(json.dumps(data, ensure_ascii=False))
        return 0
    except HTTPError as error:
        print("Credential operation rejected: HTTP " + str(error.code), file=sys.stderr)
    except (URLError, TimeoutError):
        print("Transport failed; check credential list before retrying issuance. Do not print raw responses.", file=sys.stderr)
    except (OSError, ValueError, KeyError):
        print("Credential operation failed; check arguments, private files and server availability. Existing files are not overwritten.", file=sys.stderr)
    finally:
        if reserved is not None:
            os.close(reserved)
            # Leave the reserved file in place: a transport failure may have issued a credential.
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
