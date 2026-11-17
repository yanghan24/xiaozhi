#!/usr/bin/env python3
"""Check the deployed management HTTP boundary; no database side channels."""
import argparse
import json
import sys
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:18002")
    args = parser.parse_args()
    base = args.url.rstrip("/")
    # Local verification must not go through the workstation's outbound proxy.
    client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with client.open(base + "/", timeout=10) as response:
            html = response.read().decode()
            if response.status != 200 or '<div id="app"' not in html:
                raise ValueError("management HTML did not contain the Vue application")
        with client.open(base + "/xiaozhi/user/pub-config", timeout=15) as response:
            result = json.load(response)
            if result.get("code") != 0 or not result.get("data", {}).get("sm2PublicKey"):
                raise ValueError("public configuration did not return initialized authentication configuration")
        try:
            with client.open(base + "/xiaozhi/admin/params/page?page=1&limit=1", timeout=10) as response:
                result = json.load(response)
                if result.get("code") != 401:
                    raise ValueError("anonymous parameter access was not rejected with code 401")
        except urllib.error.HTTPError as error:
            if error.code != 401:
                raise
        print("PASS: management HTML, initialized API configuration, anonymous access rejection")
        print("Browser login and configuration readback require the separate browser check.")
    except (OSError, ValueError) as error:
        print(f"FAIL: {base}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
