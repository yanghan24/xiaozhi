import Vue from 'vue';
import { parseComponent } from 'vue-template-compiler';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import source from '../src/components/ProductAsrSettings.vue?raw';

const script = parseComponent(source).script.content.replace(/^import .*;$/gm, '').replace('export default', 'return');
const component = new Function('getServiceUrl', script)(() => 'http://localhost/xiaozhi');
let vm;
beforeEach(() => {
  vm = new Vue(component);
  vm.$store = { getters: { getToken: JSON.stringify({ token: 'synthetic-admin' }) } };
  vm.$message = { success: vi.fn() };
  vi.stubGlobal('fetch', vi.fn());
});
afterEach(() => { vm.$destroy(); vi.unstubAllGlobals(); });
function response(data) {
  return { ok: true, json: async () => ({ code: 0, data }) };
}
it('loads only credential presence and preserves stored credentials on selection changes', async () => {
  fetch.mockResolvedValueOnce(response({ provider: 'market', revision: 4, xfyunConfigured: true }));
  await vm.load(); vm.provider = 'xfyun';
  fetch.mockResolvedValueOnce(response({ provider: 'xfyun', revision: 5, xfyunConfigured: true }));
  await vm.save();
  expect(JSON.parse(fetch.mock.calls[1][1].body)).toEqual({ provider: 'xfyun', revision: 4, appId: '', apiKey: '', apiSecret: '' });
  expect(vm.revision).toBe(5);
  expect(vm.$message.success).toHaveBeenCalledOnce();
});
it('clears newly entered credentials after a successful save', async () => {
  Object.assign(vm, { provider: 'xfyun', revision: 0, appId: 'app', apiKey: 'key', apiSecret: 'secret' });
  fetch.mockResolvedValue(response({ provider: 'xfyun', revision: 1, xfyunConfigured: true }));
  await vm.save();
  expect(vm.apiKey).toBe(''); expect(vm.apiSecret).toBe(''); expect(vm.appId).toBe('');
});
it('does not claim success or silently retry a conflict', async () => {
  vm.revision = 1; fetch.mockResolvedValue({ ok: false, status: 409 });
  await vm.save();
  expect(vm.error).toContain('其他管理员'); expect(fetch).toHaveBeenCalledOnce();
  expect(vm.$message.success).not.toHaveBeenCalled();
});
it('disables saving when the authoritative read fails', async () => {
  fetch.mockRejectedValue(new TypeError('Failed to fetch'));
  await vm.load(); await vm.save();
  expect(vm.revision).toBe(null); expect(fetch).toHaveBeenCalledOnce();
});
