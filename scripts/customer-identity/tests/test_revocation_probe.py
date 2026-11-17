import contextlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
from pathlib import Path
import socket
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import revocation_probe as probe


class ProbeTest(unittest.TestCase):
    def setUp(self):
        self.calls = []
        self.mode = 'success'
        self.identity = 'test-subject'
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def handle_request(self):
                owner.calls.append((self.command, self.path))
                status, body = 200, {}
                if self.path == '/oidc/me':
                    body = {'sub': owner.identity}
                elif self.path == '/api/application-credentials':
                    status = 201
                    body = {'code': 20000, 'data': {'subject': 'test-subject',
                            'client_id': 'test-client', 'api_key': 'SECRET_KEY'}}
                elif self.command == 'DELETE':
                    if owner.mode == 'lost':
                        self.connection.shutdown(socket.SHUT_RDWR)
                        self.connection.close()
                        return
                    if owner.mode == 'timeout':
                        time.sleep(0.2)
                    if owner.mode == 'expired':
                        status = 401
                    elif owner.mode == 'server':
                        status = 503
                    elif owner.mode == 'redirect':
                        status = 307
                    elif owner.mode == 'malformed':
                        body = ['SECRET_TOKEN']
                    else:
                        body = {'code': 20000, 'data': {'revoked': True}}
                self.send_response(status)
                if status == 307:
                    self.send_header('Location', owner.origin + '/leaked')
                self.end_headers()
                try:
                    self.wfile.write(json.dumps(body).encode())
                except (BrokenPipeError, ConnectionResetError):
                    pass

            do_GET = do_POST = do_DELETE = handle_request

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.origin = 'http://127.0.0.1:' + str(self.server.server_port)
        self.worker = threading.Thread(target=self.server.serve_forever,
                                       kwargs={'poll_interval': 0.01}, daemon=True)
        self.worker.start()
        self.tmp = tempfile.TemporaryDirectory()
        self.state = Path(self.tmp.name) / 'round.json'
        self.market = probe.Market(self.origin, timeout=0.05)
        self.config = {'issuer': probe.ORIGIN + '/oidc', 'client_id': 'test-client'}
        self.session = {'client_id': 'test-client', 'subject': 'test-subject',
                        'access_token': 'SECRET_TOKEN', 'access_expires_at': time.time() + 300}

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.worker.join()
        self.tmp.cleanup()

    def prepare(self):
        return probe.prepare(self.market, self.config, self.session, self.state)

    def test_success_has_durable_claim_and_no_repeat(self):
        self.prepare()
        result = probe.execute(self.market, self.state)
        self.assertEqual(result['outcome'], 'confirmed_revoked')
        self.assertEqual(probe.private_read(str(self.state) + '.result'), result)
        with self.assertRaises(FileExistsError):
            probe.execute(self.market, self.state)
        self.assertEqual(self.calls.count(('DELETE', '/api/oidc/authorization')), 1)

    def test_expired_probe_reaches_delete_without_repeating_preflight(self):
        self.prepare()
        self.mode = 'expired'
        with patch('revocation_probe.time.time', return_value=self.session['access_expires_at'] + 1):
            result = probe.execute(self.market, self.state, require_expired=True)
        self.assertEqual(result['http'], 401)
        self.assertEqual(result['outcome'], 'unconfirmed')
        self.assertEqual(self.calls.count(('GET', '/oidc/me')), 1)
        self.assertEqual(self.calls.count(('DELETE', '/api/oidc/authorization')), 1)

    def test_unexpired_round_is_not_sent_or_claimed(self):
        self.prepare()
        with self.assertRaises(ValueError):
            probe.execute(self.market, self.state, require_expired=True)
        self.assertFalse(Path(str(self.state) + '.attempt').exists())
        self.assertFalse(any(method == 'DELETE' for method, _ in self.calls))

    def test_error_responses_and_lost_results_never_retry_or_leak(self):
        for mode, expected in [('server', 503), ('lost', None), ('timeout', None),
                               ('redirect', 307), ('malformed', 200)]:
            with self.subTest(mode=mode):
                self.state = Path(self.tmp.name) / (mode + '.json')
                self.prepare()
                self.mode = mode
                before = len(self.calls)
                result = probe.execute(self.market, self.state)
                self.assertEqual(result['outcome'], 'unconfirmed')
                self.assertEqual(result['http'], expected)
                with self.assertRaises(FileExistsError):
                    probe.execute(self.market, self.state)
                self.assertEqual(self.calls[before:], [('DELETE', '/api/oidc/authorization')])
                self.assertNotIn('SECRET', json.dumps(result))

    def test_existing_claim_blocks_after_process_restart(self):
        self.prepare()
        probe.private_create(str(self.state) + '.attempt', {'outcome': 'unknown'})
        with self.assertRaises(FileExistsError):
            probe.execute(probe.Market(self.origin), self.state)
        self.assertFalse(any(method == 'DELETE' for method, _ in self.calls))

    def test_wrong_identity_cannot_prepare(self):
        self.identity = 'different-subject'
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.state.exists())
        self.assertEqual(self.calls, [('GET', '/oidc/me')])

    def test_readonly_observation_does_not_claim_revocation(self):
        self.prepare()
        result = probe.observe(self.market, self.state)
        self.assertEqual(result['old_key_http'], 200)
        self.assertEqual(result['authorization_outcome'], 'requires_correlated_evidence')
        self.assertFalse(any(method == 'DELETE' for method, _ in self.calls))

    def test_private_file_and_origin_guards(self):
        self.prepare()
        self.assertEqual(self.state.stat().st_mode & 0o777, 0o600)
        self.state.chmod(0o644)
        with self.assertRaises(ValueError):
            probe.execute(self.market, self.state)
        for origin in ['https://example.com', 'http://localhost:1234', self.origin + '/other']:
            with self.assertRaises(ValueError):
                probe.Market(origin)

    def test_concurrent_operators_send_at_most_once(self):
        self.prepare()
        outcomes = []

        def attempt():
            try:
                outcomes.append(probe.execute(self.market, self.state)['outcome'])
            except FileExistsError:
                outcomes.append('blocked')

        threads = [threading.Thread(target=attempt) for _ in range(2)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.assertCountEqual(outcomes, ['confirmed_revoked', 'blocked'])
        self.assertEqual(self.calls.count(('DELETE', '/api/oidc/authorization')), 1)

    def test_cli_requires_confirmation_and_redacts_errors(self):
        output = io.StringIO()
        with patch('sys.argv', ['probe', 'execute', '--state', 'SECRET_PATH']), contextlib.redirect_stdout(output):
            self.assertEqual(probe.main(), 1)
        self.assertNotIn('SECRET', output.getvalue())


if __name__ == '__main__':
    unittest.main()
