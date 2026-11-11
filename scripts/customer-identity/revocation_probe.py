#!/usr/bin/env python3
"""Operator-only shared-authorization probe; never used by device sign-out."""
import argparse
import json
import os
from pathlib import Path
import stat
import time
import urllib.error
import urllib.request


ORIGIN = 'https://aimarket.hncig.cn'
LIMIT = 1048576


def private_read(path):
    fd = os.open(str(path), os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd) as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_mode & 0o077:
            raise ValueError('Private regular file required')
        return json.loads(stream.read(LIMIT))


def private_create(path, value):
    fd = os.open(str(path), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w') as stream:
        json.dump(value, stream)
        stream.flush()
        os.fsync(stream.fileno())
    directory = os.open(str(Path(path).parent), os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


class Market:
    def __init__(self, origin=ORIGIN, timeout=15):
        # Alternate origins exist only for loopback fake-server regression.
        from urllib.parse import urlsplit
        parsed = urlsplit(origin)
        if origin != ORIGIN and not (
                parsed.scheme == 'http' and parsed.hostname == '127.0.0.1'
                and parsed.port and not parsed.username and not parsed.password
                and not parsed.path and not parsed.query and not parsed.fragment):
            raise ValueError('Unsupported origin')
        self.origin, self.timeout = origin, timeout
        self.opener = urllib.request.build_opener(
            urllib.request.ProxyHandler({}), NoRedirect())

    def call(self, path, method, credential):
        request = urllib.request.Request(self.origin + path, method=method,
                                        headers={'Authorization': 'Bearer ' + credential})
        try:
            response = self.opener.open(request, timeout=self.timeout)
        except urllib.error.HTTPError as error:
            response = error
        except Exception:
            return None, None
        with response:
            status = response.code
            try:
                raw = response.read(LIMIT + 1)
                body = json.loads(raw) if len(raw) <= LIMIT else None
            except Exception:
                body = None
            return status, body if isinstance(body, dict) else None


def prepare(market, config, session, state_path):
    if config['issuer'] != ORIGIN + '/oidc' or session['client_id'] != config['client_id']:
        raise ValueError('Application mismatch')
    if not isinstance(session['access_expires_at'], (int, float)) or session['access_expires_at'] <= time.time():
        raise ValueError('Prepare with an unexpired token')
    status, body = market.call('/oidc/me', 'GET', session['access_token'])
    if status != 200 or not body or body.get('sub') != session['subject']:
        raise ValueError('Identity not verified')
    status, body = market.call('/api/application-credentials', 'POST', session['access_token'])
    data = body.get('data') if body else None
    if (status != 201 or body.get('code') != 20000 or not isinstance(data, dict)
            or data.get('subject') != session['subject'] or data.get('client_id') != config['client_id']
            or not isinstance(data.get('api_key'), str) or not data['api_key']):
        raise ValueError('Application credential not verified')
    status, _ = market.call('/v1/models', 'GET', data['api_key'])
    if status != 200:
        raise ValueError('Key baseline failed')
    private_create(state_path, dict(session, api_key=data['api_key'], origin=market.origin,
                                    prepared_at=time.time()))
    return {'phase': 'prepared', 'key_baseline_http': status}


def execute(market, state_path, require_expired=False):
    state = private_read(state_path)
    if state['origin'] != market.origin:
        raise ValueError('Origin mismatch')
    if require_expired and time.time() <= state['access_expires_at']:
        raise ValueError('Token has not naturally expired')
    # Durable claim BEFORE sending. A crash can sacrifice a round, never replay it.
    private_create(str(state_path) + '.attempt', {'claimed_at': time.time(), 'outcome': 'unknown'})
    status, body = market.call('/api/oidc/authorization', 'DELETE', state['access_token'])
    data = body.get('data') if body else None
    confirmed = (status == 200 and body.get('code') == 20000
                 and isinstance(data, dict) and data.get('revoked') is True) if body else False
    result = {'phase': 'attempted', 'http': status,
              'outcome': 'confirmed_revoked' if confirmed else 'unconfirmed',
              'automatic_retry': False}
    # Never persist upstream bodies, identifiers, URLs, or tokens in evidence.
    private_create(str(state_path) + '.result', result)
    return result


def observe(market, state_path):
    state = private_read(state_path)
    if state['origin'] != market.origin:
        raise ValueError('Origin mismatch')
    status, _ = market.call('/v1/models', 'GET', state['api_key'])
    return {'phase': 'observed', 'old_key_http': status,
            'authorization_outcome': 'requires_correlated_evidence'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=['prepare', 'execute', 'observe'])
    parser.add_argument('--state', required=True)
    parser.add_argument('--client-config')
    parser.add_argument('--private-test-session')
    parser.add_argument('--confirm-shared-test-account', action='store_true')
    parser.add_argument('--require-expired', action='store_true')
    args = parser.parse_args()
    try:
        market = Market()  # CLI always uses the real fixed TLS endpoint.
        if args.phase == 'prepare':
            if not args.confirm_shared_test_account:
                raise ValueError('Dedicated account confirmation required')
            result = prepare(market, private_read(args.client_config),
                             private_read(args.private_test_session), args.state)
        elif args.phase == 'execute':
            if not args.confirm_shared_test_account:
                raise ValueError('Shared revocation confirmation required')
            result = execute(market, args.state, args.require_expired)
        else:
            result = observe(market, args.state)
        print(json.dumps(result))
        return 2 if result.get('outcome') == 'unconfirmed' else 0
    except Exception:
        print(json.dumps({'outcome': 'stopped', 'automatic_retry': False,
                          'action': 'inspect_private_attempt_and_result; do_not_delete_claim'}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
