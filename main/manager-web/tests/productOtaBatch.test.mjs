import Vue from 'vue';
import { mount } from '@vue/test-utils';
import { parseComponent, compileToFunctions } from 'vue-template-compiler';
import { afterEach, it, expect, vi } from 'vitest';
import source from '../src/components/ProductOtaPush.vue?raw';

const parsed = parseComponent(source);
const component = new Function('getServiceUrl', parsed.script.content.replace(/^import .*;$/gm, '').replace('export default', 'return'))(() => '/xiaozhi');
Object.assign(component, compileToFunctions(parsed.template.content));
const response = data => ({ ok: true, json: async () => ({ code: 0, data }) });
const settle = async () => { for (let i = 0; i < 20; i++) await Vue.nextTick(); };
const firmware = { firmware_id: 'fixed-build', app_version: '2.2.6', elf_sha256: 'target-elf' };
let wrapper;
afterEach(() => { wrapper?.destroy(); vi.unstubAllGlobals(); });
const open = async () => {
  vi.stubGlobal('fetch', vi.fn(async path => response(path.includes('/devices?')
    ? { items: [{ device_id: path.includes('page=2') ? 'online-C' : 'online-A', status: 'current' }], total: 51 }
    : [])));
  wrapper = mount(component, { propsData: { firmware }, mocks: { $store: { getters: { getToken: '{}' } } } });
  await settle();
};
it('preserves selected devices across pages and previews their exact combined set', async () => {
  await open();
  await wrapper.find('[data-test=device-online-A]').setChecked();
  await wrapper.find('[data-test=next-page]').trigger('click'); await settle();
  await wrapper.find('[data-test=device-online-C]').setChecked();
  fetch.mockResolvedValueOnce(response({ preview_id: 'batch-preview', eligible_count: 2, items: [
    { device_id: 'online-A', eligible: true, reason: 'eligible' },
    { device_id: 'online-C', eligible: true, reason: 'eligible' }
  ], expires_at_ms: Date.now() + 60000 }));
  await wrapper.find('[data-test=preview]').trigger('click'); await settle();
  const request = fetch.mock.calls.find(([path]) => path.endsWith('/batch-preview'));
  expect(request).toBeDefined();
  expect(JSON.parse(request[1].body)).toEqual({ mode: 'selected', device_ids: ['online-A', 'online-C'], firmware_id: 'fixed-build' });
  expect(wrapper.text()).toContain('online-A');
  expect(wrapper.text()).toContain('online-C');
  expect(wrapper.find('[data-test=confirm]').attributes('disabled')).toBeDefined();
});
it('blocks uncertain batch targets until read-only history recovers the request', async () => {
  await open();
  await wrapper.find('[data-test=device-online-A]').setChecked();
  await wrapper.find('[data-test=next-page]').trigger('click'); await settle();
  await wrapper.find('[data-test=device-online-C]').setChecked();
  fetch.mockResolvedValueOnce(response({ preview_id: 'lost-preview', eligible_count: 2, items: [
    { device_id: 'online-A', eligible: true, reason: 'eligible' },
    { device_id: 'online-C', eligible: true, reason: 'eligible' }
  ], expires_at_ms: Date.now() + 60000 }));
  await wrapper.find('[data-test=preview]').trigger('click'); await settle();
  await wrapper.find('[data-test=acknowledge]').setChecked();
  fetch.mockRejectedValueOnce(new Error('connection lost'));
  await wrapper.find('[data-test=confirm]').trigger('click'); await settle();
  expect(wrapper.text()).toContain('请求响应丢失，结果未知');
  expect(wrapper.find('[data-test=preview]').attributes('disabled')).toBeDefined();
  expect(wrapper.find('[data-test=preview-all]').attributes('disabled')).toBeDefined();
  const requestKey = JSON.parse(fetch.mock.calls.find(([path, options]) => path.endsWith('/batches') && options.method === 'POST')[1].body).request_key;
  fetch.mockResolvedValueOnce(response({ items: [{ device_id: 'online-C', status: 'current' }], total: 51 }))
    .mockResolvedValueOnce(response([])).mockResolvedValueOnce(response([{ batch_id: 'saved-batch', request_key: requestKey, status: 'completed', items: [
      { device_id: 'online-A', status: 'result_unknown', reason: 'bridge_response_unavailable' },
      { device_id: 'online-C', status: 'not_sent', reason: 'offline' }
    ] }]));
  await wrapper.find('button').trigger('click'); await settle();
  expect(wrapper.text()).not.toContain('请求响应丢失，结果未知');
  expect(wrapper.text()).toContain('结果未知 · 禁止重发');
  expect(fetch.mock.calls.filter(([path, options]) => path.endsWith('/batches') && options.method === 'POST')).toHaveLength(1);
});
it('asks the server for all online targets and confirms only the fixed preview once', async () => {
  await open();
  fetch.mockResolvedValueOnce(response({ preview_id: 'all-preview', eligible_count: 1, items: [
    { device_id: 'off-page-Z', eligible: true, reason: 'eligible' },
    { device_id: 'offline-B', eligible: false, reason: 'offline' },
    { device_id: 'same-C', eligible: false, reason: 'already_running' }
  ], expires_at_ms: Date.now() + 60000 }));
  await wrapper.find('[data-test=preview-all]').trigger('click'); await settle();
  const request = fetch.mock.calls.find(([path]) => path.endsWith('/batch-preview'));
  expect(JSON.parse(request[1].body)).toEqual({ mode: 'all_online', device_ids: [], firmware_id: 'fixed-build' });
  expect(wrapper.text()).toContain('off-page-Z');
  expect(wrapper.text()).toContain('排除 2 台');
  expect(wrapper.text()).toContain('无需重装');
  await wrapper.find('[data-test=acknowledge]').setChecked();
  let finish;
  fetch.mockReturnValueOnce(new Promise(resolve => { finish = resolve; }));
  await wrapper.find('[data-test=confirm]').trigger('click');
  await wrapper.find('[data-test=confirm]').trigger('click');
  const confirms = fetch.mock.calls.filter(([path, options]) => path.endsWith('/batches') && options.method === 'POST');
  expect(confirms).toHaveLength(1);
  expect(JSON.parse(confirms[0][1].body)).toEqual({ preview_id: 'all-preview', request_key: expect.stringMatching(/^[a-f0-9]{32}$/) });
  finish(response({ batch_id: 'batch-1', status: 'completed', firmware_id: 'fixed-build', items: [
    { device_id: 'off-page-Z', status: 'accepted_unconfirmed', operation_id: 'operation-Z' },
    { device_id: 'offline-B', status: 'not_sent', reason: 'offline' }
  ] }));
  await settle();
  expect(wrapper.text()).toContain('batch-1');
  expect(wrapper.text()).toContain('已接受 · 安装待核验');
  expect(wrapper.text()).toContain('未发送');
  expect(wrapper.find('[data-test=confirm]').exists()).toBe(false);
});
