import Vue from 'vue';
import { mount } from '@vue/test-utils';
import { parseComponent, compileToFunctions } from 'vue-template-compiler';
import { beforeEach, afterEach, it, expect, vi } from 'vitest';
import source from '../src/components/ProductOtaPush.vue?raw';

const parsed = parseComponent(source);
const component = new Function('getServiceUrl', parsed.script.content.replace(/^import .*;$/gm, '').replace('export default', 'return'))(() => '/xiaozhi');
Object.assign(component, compileToFunctions(parsed.template.content));
const response = data => ({ ok: true, json: async () => ({ code: 0, data }) });
const settle = async () => { for (let i = 0; i < 15; i++) await Vue.nextTick(); };
const firmware = { firmware_id: 'fixed-build', app_version: '2.2.6', elf_sha256: 'target-elf', verification_status: 'verified' };
let wrapper;
beforeEach(() => vi.stubGlobal('fetch', vi.fn(async () => response([]))));
afterEach(() => { wrapper?.destroy(); vi.unstubAllGlobals(); });
const open = async () => {
  fetch.mockResolvedValueOnce(response({ items: [
    { device_id: 'online-A', display_name: '设备 A', status: 'current' },
    { device_id: 'offline-B', display_name: '设备 B', status: 'offline' }
  ], total: 51, page: 1, limit: 50 })).mockResolvedValueOnce(response([]));
  wrapper = mount(component, { propsData: { firmware }, mocks: { $store: { getters: { getToken: JSON.stringify({ token: 'fixture' }) } } } });
  await settle();
};
const preview = async () => {
  await open();
  await wrapper.find('[data-test=device-online-A]').setChecked();
  fetch.mockResolvedValueOnce(response({ preview_id: 'preview-1', eligible: true, device_id: 'online-A', firmware_id: 'fixed-build', expires_at_ms: Date.now() + 60000 }));
  await wrapper.find('[data-test=preview]').trigger('click'); await settle();
};
it('shows current availability and previews only the selected device and imported firmware', async () => {
  await open();
  expect(wrapper.text()).toContain('离线');
  expect(wrapper.find('[data-test=device-offline-B]').attributes('disabled')).toBeDefined();
  expect(wrapper.find('[data-test=preview]').attributes('disabled')).toBeDefined();
  await wrapper.find('[data-test=device-online-A]').setChecked();
  fetch.mockResolvedValueOnce(response({ preview_id: 'preview-1', eligible: true, device_id: 'online-A', firmware_id: 'fixed-build', current_version: '2.2.5', current_elf_sha256: 'old-elf', target_version: '2.2.6', target_elf_sha256: 'target-elf', expires_at_ms: Date.now() + 60000 }));
  await wrapper.find('[data-test=preview]').trigger('click'); await settle();
  expect(JSON.parse(fetch.mock.calls.find(([path]) => path.endsWith('/preview'))[1].body)).toEqual({ device_id: 'online-A', firmware_id: 'fixed-build' });
  expect(wrapper.text()).toContain('old-elf');
  expect(wrapper.text()).toContain('立即');
  expect(wrapper.text()).toContain('重启');
  expect(wrapper.find('[data-test=confirm]').attributes('disabled')).toBeDefined();
});
it('requires explicit confirmation and displays acceptance without claiming installation or resending', async () => {
  await preview();
  await wrapper.find('[data-test=acknowledge]').setChecked();
  let finish;
  fetch.mockReturnValueOnce(new Promise(resolve => { finish = resolve; }));
  await wrapper.find('[data-test=confirm]').trigger('click');
  await wrapper.find('[data-test=confirm]').trigger('click');
  const posts = fetch.mock.calls.filter(([, options]) => options.body && JSON.parse(options.body).request_key);
  expect(posts).toHaveLength(1);
  expect(JSON.parse(posts[0][1].body)).toEqual({ preview_id: 'preview-1', request_key: expect.stringMatching(/^[a-f0-9]{32}$/) });
  finish(response({ operation_id: 'operation-1', device_id: 'online-A', firmware_id: 'fixed-build', status: 'accepted_unconfirmed', observation_status: 'unconfirmed' }));
  await settle();
  expect(wrapper.text()).toContain('已接受 · 安装待核验');
  expect(wrapper.find('[data-test=confirm]').exists()).toBe(false);
  expect(wrapper.find('[data-test=preview]').attributes('disabled')).toBeDefined();
});
it('discards a preview response if the operator changes the selected firmware while it is checked', async () => {
  await open();
  await wrapper.find('[data-test=device-online-A]').setChecked();
  let finish;
  fetch.mockReturnValueOnce(new Promise(resolve => { finish = resolve; }));
  await wrapper.find('[data-test=preview]').trigger('click');
  await wrapper.setProps({ firmware: { ...firmware, firmware_id: 'other-build' } });
  finish(response({ preview_id: 'stale-preview', eligible: true, device_id: 'online-A', firmware_id: 'fixed-build', expires_at_ms: Date.now() + 60000 }));
  await settle();
  expect(wrapper.find('[data-test=confirm]').exists()).toBe(false);
});
it('keeps a lost confirmation response unknown and only reads results when refreshed', async () => {
  await preview();
  await wrapper.find('[data-test=acknowledge]').setChecked();
  fetch.mockRejectedValueOnce(new Error('network unavailable'));
  await wrapper.find('[data-test=confirm]').trigger('click'); await settle();
  expect(wrapper.text()).toContain('请求响应丢失，结果未知');
  expect(wrapper.find('[data-test=preview]').attributes('disabled')).toBeDefined();
  const requestKey = JSON.parse(fetch.mock.calls.find(([path, options]) => path.endsWith('/operations') && options.method === 'POST')[1].body).request_key;
  fetch.mockResolvedValueOnce(response({ items: [{ device_id: 'online-A', status: 'current' }], total: 1 }))
    .mockResolvedValueOnce(response([{ operation_id: 'operation-1', device_id: 'online-A', request_key: requestKey, status: 'result_unknown', firmware_id: 'fixed-build' }]));
  await wrapper.find('button').trigger('click'); await settle();
  expect(wrapper.text()).toContain('结果未知 · 禁止重发');
  expect(wrapper.text()).not.toContain('请求响应丢失，结果未知');
  expect(fetch.mock.calls.filter(([path, options]) => path.endsWith('/operations') && options.method === 'POST')).toHaveLength(1);
});
it('shows exclusions without confirmation and requests the next device page from the server', async () => {
  await open();
  await wrapper.find('[data-test=device-online-A]').setChecked();
  fetch.mockResolvedValueOnce(response({ eligible: false, reason: 'missing_system_info', device_id: 'online-A', firmware_id: 'fixed-build' }));
  await wrapper.find('[data-test=preview]').trigger('click'); await settle();
  expect(wrapper.text()).toContain('缺少可信硬件信息');
  expect(wrapper.find('[data-test=confirm]').exists()).toBe(false);
  fetch.mockResolvedValueOnce(response({ items: [{ device_id: 'online-C', status: 'current' }], total: 51 })).mockResolvedValueOnce(response([]));
  await wrapper.find('[data-test=next-page]').trigger('click'); await settle();
  expect(fetch.mock.calls.some(([path]) => path === '/xiaozhi/admin/product-ota/devices?page=2&limit=50')).toBe(true);
  expect(wrapper.text()).toContain('online-C');
  expect(wrapper.find('[data-test=preview]').attributes('disabled')).toBeUndefined();
});
it('does not submit an expired confirmation and reports lost operator access', async () => {
  await open();
  await wrapper.find('[data-test=device-online-A]').setChecked();
  fetch.mockResolvedValueOnce(response({ preview_id: 'expired', eligible: true, device_id: 'online-A', firmware_id: 'fixed-build', expires_at_ms: Date.now() - 1000 }));
  await wrapper.find('[data-test=preview]').trigger('click'); await settle();
  await wrapper.find('[data-test=acknowledge]').setChecked();
  await wrapper.find('[data-test=confirm]').trigger('click'); await settle();
  expect(wrapper.text()).toContain('预览已过期');
  expect(fetch.mock.calls.filter(([, options]) => options.body && JSON.parse(options.body).request_key)).toHaveLength(0);
  fetch.mockResolvedValueOnce({ ok: false, status: 403, json: async () => ({ code: 'OPERATOR_REQUIRED' }) });
  await wrapper.find('[data-test=preview]').trigger('click'); await settle();
  expect(wrapper.text()).toContain('需要有效的运营管理员登录权限');
});
it('keeps unknown transport visible after observing target firmware and allows a new compatibility check', async () => {
  fetch.mockResolvedValueOnce(response({ items: [{ device_id: 'online-A', status: 'current' }], total: 1 }))
    .mockResolvedValueOnce(response([{ operation_id: 'old-operation', device_id: 'online-A', status: 'result_unknown', observation_status: 'target_running' }]));
  wrapper = mount(component, { propsData: { firmware }, mocks: { $store: { getters: { getToken: '{}' } } } });
  await settle();
  expect(wrapper.text()).toContain('结果未知');
  expect(wrapper.text()).toContain('已观察到目标固件运行');
  await wrapper.find('[data-test=device-online-A]').setChecked();
  expect(wrapper.find('[data-test=preview]').attributes('disabled')).toBeUndefined();
});
