#!/usr/bin/env python3
"""Operator-only #11 test-account check. Never called by the product sign-out path."""
import argparse
import json
from pathlib import Path
import stat
import urllib.error
import urllib.request


def private_json(name):
    path = Path(name)
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_mode & 0o077:
        raise ValueError("Input must be a private regular file (0600)")
    return json.loads(path.read_text())


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client-config', required=True)
    parser.add_argument('--private-test-session', required=True,
                        help='Private JSON containing access_token, subject, client_id from the dedicated test account')
    parser.add_argument('--confirm-revoke-test-subject', required=True)
    args = parser.parse_args()
    config = private_json(args.client_config)
    session = private_json(args.private_test_session)
    if config['issuer'] != 'https://aimarket.hncig.cn/oidc':
        raise ValueError('Unexpected issuer')
    if session['subject'] != args.confirm_revoke_test_subject or session['client_id'] != config['client_id']:
        raise ValueError('Test-account confirmation does not match')
    opener = urllib.request.build_opener(NoRedirect)

    def call(path, method, credential):
        request = urllib.request.Request('https://aimarket.hncig.cn' + path, method=method,
                                         headers={'Authorization': 'Bearer ' + credential})
        try:
            with opener.open(request, timeout=15) as response:
                return response.status, json.loads(response.read(1_048_576))
        except urllib.error.HTTPError as error:
            return error.code, {}

    code, identity = call('/oidc/me', 'GET', session['access_token'])
    if code != 200 or identity.get('sub') != session['subject']:
        raise ValueError('Cannot verify the dedicated test account')
    code, envelope = call('/api/application-credentials', 'POST', session['access_token'])
    data = envelope.get('data', {})
    if code != 201 or envelope.get('code') != 20000 or data.get('subject') != session['subject'] or data.get('client_id') != config['client_id']:
        raise ValueError('Cannot verify test application credential')
    key = data['api_key']
    before, _ = call('/v1/models', 'GET', key)
    if before != 200:
        raise ValueError('Test Key was not usable before revocation')
    revoked, body = call('/api/oidc/authorization', 'DELETE', session['access_token'])
    if revoked != 200 or body.get('data', {}).get('revoked') is not True:
        raise ValueError('Revocation was not confirmed; inspect test grant before retrying')
    after, _ = call('/v1/models', 'GET', key)
    print(json.dumps({'before_http': before, 'revocation_http': revoked, 'after_http': after,
                      'shared_key_rejected': after in (401, 403)}))
    return 0 if after in (401, 403) else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception:
        # Exception bodies and URLs from HTTP libraries may contain sensitive context.
        print('Test-account revocation check failed; credentials were not printed.')
        raise SystemExit(1)
