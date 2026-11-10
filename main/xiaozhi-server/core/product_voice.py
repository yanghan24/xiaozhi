"""Product voice authority and transport gate, separate from legacy self-reported identity."""
import asyncio
import json
import re
import threading
import os
import urllib.request
import urllib.error
from urllib.parse import urlparse
from core.product_rejections import RejectionOutbox, RejectionStoreUnavailable


class VoiceDenied(Exception):
    def __init__(self):
        super().__init__('Product voice permission unavailable')


class ModelUnavailable(Exception):
    pass


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class VoiceAuthority:
    def __init__(self, base_url, service_key, state_directory=None):
        parsed = urlparse(base_url)
        if parsed.scheme not in ('http', 'https') or not parsed.hostname or parsed.username or parsed.query or parsed.fragment or len(service_key) < 32:
            raise ValueError('Product voice requires private authority configuration')
        self.base = base_url.rstrip('/') + '/internal/device-voice/v1/'
        self.key = service_key
        self.rejections = RejectionOutbox(self.base, self.request,
            state_directory or os.environ.get('PRODUCT_VOICE_STATE_DIR', 'data/product-voice'))

    def check_scope(self, data, order_field='permission_authorization_order'):
        try:
            scope, subject, order = data.get('authorization_scope'), data.get('authorization_subject'), data.get(order_field)
            if (not isinstance(scope,str) or not re.fullmatch(r'[A-Za-z0-9_-]{43}', scope)
                    or not isinstance(subject,str) or not re.fullmatch(r'[a-f0-9]{64}', subject)
                    or type(order) is not int or order<0 or self.rejections.blocked(scope,subject,order)):
                raise VoiceDenied()
        except RejectionStoreUnavailable:
            raise VoiceDenied() from None

    def request(self, action, body):
        request = urllib.request.Request(self.base + action, data=json.dumps(body).encode(), headers={
            'X-Voice-Service-Key': self.key, 'Content-Type': 'application/json'}, method='POST')
        try:
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
            with opener.open(request, timeout=2) as response:
                if response.status != 200:
                    raise VoiceDenied()
                payload = response.read(16385)
                if len(payload) > 16384:
                    raise VoiceDenied()
                data = json.loads(payload)
                if not isinstance(data, dict):
                    raise VoiceDenied()
                return data
        except Exception:
            # Never include headers, request bodies or transport errors carrying credentials.
            raise VoiceDenied() from None

    async def open(self, headers, path):
        headers = {k.lower(): v for k, v in headers.items()}
        authorization = headers.get('authorization', '')
        device = headers.get('device-id', '')
        if path != '/xiaozhi/v1/' or not re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}', device) or not re.fullmatch(r'Bearer [A-Za-z0-9_-]{43}', authorization):
            raise VoiceDenied()
        data = await asyncio.to_thread(self.request, 'open', {'device_id': device, 'token': authorization[7:]})
        if not re.fullmatch(r'[A-Za-z0-9_-]{43}', data.get('session', '')) or type(data.get('permission_version')) is not int:
            raise VoiceDenied()
        gate = VoiceGate(self, data['session'], data['permission_version'], data)
        try:
            self.check_scope(gate.grant)
        except VoiceDenied:
            await gate.close()
            raise
        return gate


class VoiceGate:
    def __init__(self, authority, session, version, grant):
        self.authority = authority
        self.session = session
        self.version = version
        self.ended = threading.Event()
        self.grant = grant

    def check(self):
        if self.ended.is_set():
            raise VoiceDenied()
        try:
            self.authority.check_scope(self.grant)
            result = self.authority.request('check', {'session': self.session})
            if result.get('allowed') is not True or result.get('permission_version') != self.version or self.ended.is_set():
                raise VoiceDenied()
            self.authority.check_scope(self.grant)
            self.authority.check_scope(result)
            self.grant = result
        except VoiceDenied:
            self.ended.set()
            raise

    async def check_async(self):
        await asyncio.to_thread(self.check)

    def model_access(self):
        self.check()
        try:
            result = self.authority.request('model-access', {'session': self.session})
            if self.ended.is_set() or result.get('model') != 'qwen3.8-flash' or not result.get('api_key') or not result.get('rejection_receipt'):
                raise VoiceDenied()
            self.authority.check_scope(result, 'authorization_order')
            self.authority.check_scope(self.grant)
            return result
        except VoiceDenied:
            self.ended.set()
            raise

    def key_rejected(self, access):
        # Report the generation actually used, never whichever grant is current now.
        self.ended.set()
        try:
            self.authority.rejections.reject(access['authorization_scope'], access['rejection_receipt'], access['authorization_subject'], access['authorization_order'])
        except RejectionStoreUnavailable:
            pass  # The shared store failure also blocks every gate for this authority.
        raise VoiceDenied()

    async def close(self):
        self.ended.set()
        try:
            await asyncio.to_thread(self.authority.request, 'close', {'session': self.session})
        except VoiceDenied:
            pass

    async def watch(self, socket, on_revoke):
        try:
            while not self.ended.is_set():
                await self.check_async()
                await asyncio.sleep(.2)
        except VoiceDenied:
            pass
        on_revoke()
        await socket.close(code=1008, reason='Device permission changed')


class GuardedSocket:
    """All protocol text and binary output crosses the current authoritative permission."""
    def __init__(self, socket, gate):
        self._socket = socket
        self._gate = gate

    def __getattr__(self, name):
        return getattr(self._socket, name)

    async def send(self, message, *args, **kwargs):
        await self._gate.check_async()
        if self._gate.ended.is_set():
            raise VoiceDenied()
        return await self._socket.send(message, *args, **kwargs)

    async def __aiter__(self):
        async for message in self._socket:
            await self._gate.check_async()
            yield message


class MarketModel:
    """Direct AI-market chat completions using this device owner's current backend Key."""
    def __init__(self, gate):
        self.gate = gate

    def response(self, session_id, dialogue, **kwargs):
        access = self.gate.model_access()
        body = {'model': access['model'], 'messages': dialogue, 'stream': True}
        request = urllib.request.Request(access['base_url'].rstrip('/') + '/chat/completions',
            data=json.dumps(body, ensure_ascii=False).encode(), headers={
                'Authorization': 'Bearer ' + access['api_key'], 'Content-Type': 'application/json'}, method='POST')
        try:
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
            with opener.open(request, timeout=30) as response:
                for raw in response:
                    self.gate.check()
                    if not raw.startswith(b'data:'):
                        continue
                    payload = raw[5:].strip()
                    if payload == b'[DONE]':
                        return
                    item = json.loads(payload)
                    if 'error' in item:
                        raise ModelUnavailable('AI market stream failed')
                    choices = item.get('choices', [])
                    if choices:
                        content = choices[0].get('delta', {}).get('content')
                        if isinstance(content, str) and content:
                            yield content
                raise ModelUnavailable('AI market stream ended without completion')
        except urllib.error.HTTPError as error:
            if error.code == 401:
                self.gate.key_rejected(access)
            # Resource/credit failures do not revoke identity or shared authorization.
            raise ModelUnavailable('AI market request failed (HTTP %s)' % error.code) from None
        except VoiceDenied:
            raise
        except Exception:
            raise ModelUnavailable('AI market response unavailable') from None

    def response_with_functions(self, *args, **kwargs):
        raise ModelUnavailable('Tools are not enabled for direct market model mode')
