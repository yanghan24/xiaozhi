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
afterEach(() => { wrapper?.destroy(); vi.useRealTimers(); vi.unstubAllGlobals(); });
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
it('leaves manual result lookup available when a batch status request never responds', async () => {
  vi.useFakeTimers();
  await open();
  fetch.mockResolvedValueOnce(response({ preview_id: 'stalled-preview', status: 'ready', eligible_count: 1, items: [
    { device_id: 'online-A', eligible: true, reason: 'eligible' }
  ], expires_at_ms: Date.now() + 60000 }));
  await wrapper.find('[data-test=preview-all]').trigger('click'); await settle();
  await wrapper.find('[data-test=acknowledge]').setChecked();
  fetch.mockResolvedValueOnce(response({ batch_id: 'stalled-batch', status: 'started', items: [{ device_id: 'online-A', status: 'dispatch_reserved' }] }));
  await wrapper.find('[data-test=confirm]').trigger('click'); await settle();
  fetch.mockReturnValueOnce(new Promise(() => {}));
  await vi.advanceTimersByTimeAsync(121000); await settle();
  expect(wrapper.text()).toContain('暂时无法读取批次结果');
  expect(wrapper.text()).not.toContain('正在自动读取');
  expect(wrapper.find('[data-test=refresh-batch-stalled-batch]').attributes('disabled')).toBeUndefined();
  expect(fetch.mock.calls.filter(([path, options]) => path.endsWith('/batches') && options.method === 'POST')).toHaveLength(1);
});
it('does not confirm failed preflight and discards a poll when the firmware changes', async () => {
  vi.useFakeTimers();
  await open();
  fetch.mockResolvedValueOnce(response({ preview_id: 'failed-preview', status: 'preparing', items: [], eligible_count: 0 }));
  await wrapper.find('[data-test=preview-all]').trigger('click'); await settle();
  fetch.mockResolvedValueOnce(response({ preview_id: 'failed-preview', status: 'failed', reason: 'preview_unavailable', items: [], eligible_count: 0 }));
  await vi.advanceTimersByTimeAsync(1000); await settle();
  expect(wrapper.text()).toContain('批次预检失败，未下发升级');
  expect(wrapper.find('[data-test=confirm]').exists()).toBe(false);
  fetch.mockResolvedValueOnce(response({ preview_id: 'stale-preview', status: 'preparing', items: [], eligible_count: 0 }));
  await wrapper.find('[data-test=preview-all]').trigger('click'); await settle();
  await wrapper.setProps({ firmware: { ...firmware, firmware_id: 'different-image' } });
  await vi.advanceTimersByTimeAsync(2000); await settle();
  expect(fetch.mock.calls.some(([path]) => path.endsWith('/batch-previews/stale-preview'))).toBe(false);
  expect(wrapper.find('[data-test=confirm]').exists()).toBe(false);
});
it('stops reading an unfinished batch after the polling limit and leaves manual refresh available', async () => {
  vi.useFakeTimers();
  await open();
  fetch.mockResolvedValueOnce(response({ preview_id: 'bounded-preview', status: 'ready', eligible_count: 1, items: [
    { device_id: 'online-A', eligible: true, reason: 'eligible' }
  ], expires_at_ms: Date.now() + 60000 }));
  await wrapper.find('[data-test=preview-all]').trigger('click'); await settle();
  await wrapper.find('[data-test=acknowledge]').setChecked();
  const pending = { batch_id: 'bounded-batch', status: 'started', items: [{ device_id: 'online-A', status: 'dispatch_reserved' }] };
  fetch.mockResolvedValue(response(pending));
  await wrapper.find('[data-test=confirm]').trigger('click'); await settle();
  await vi.advanceTimersByTimeAsync(121000); await settle();
  expect(wrapper.text()).toContain('自动读取已停止');
  expect(wrapper.find('[data-test=refresh-batch-bounded-batch]').attributes('disabled')).toBeUndefined();
  expect(fetch.mock.calls.filter(([path, options]) => path.endsWith('/batches') && options.method === 'POST')).toHaveLength(1);
  const reads = fetch.mock.calls.filter(([path]) => path.endsWith('/batches/bounded-batch')).length;
  expect(reads).toBeLessThanOrEqual(120);
  await vi.advanceTimersByTimeAsync(5000);
  expect(fetch.mock.calls.filter(([path]) => path.endsWith('/batches/bounded-batch'))).toHaveLength(reads);
});
it('reads an accepted asynchronous batch until finished without confirming again', async () => {
  vi.useFakeTimers();
  await open();
  fetch.mockResolvedValueOnce(response({ preview_id: 'async-preview', status: 'ready', eligible_count: 1, items: [
    { device_id: 'online-A', eligible: true, reason: 'eligible' }
  ], expires_at_ms: Date.now() + 60000 }));
  await wrapper.find('[data-test=preview-all]').trigger('click'); await settle();
  await wrapper.find('[data-test=acknowledge]').setChecked();
  fetch.mockResolvedValueOnce(response({ batch_id: 'async-batch', status: 'started', items: [{ device_id: 'online-A', status: 'dispatch_reserved' }] }));
  await wrapper.find('[data-test=confirm]').trigger('click'); await settle();
  expect(wrapper.find('[data-test=confirm]').exists()).toBe(false);
  expect(wrapper.text()).toContain('正在自动读取');
  fetch.mockResolvedValueOnce(response({ batch_id: 'async-batch', status: 'finished', items: [{ device_id: 'online-A', status: 'accepted_unconfirmed' }] }));
  await vi.advanceTimersByTimeAsync(1000); await settle();
  expect(wrapper.text()).toContain('已接受 · 安装待核验');
  expect(wrapper.text()).not.toContain('正在自动读取');
  await vi.advanceTimersByTimeAsync(3000);
  expect(fetch.mock.calls.filter(([path, options]) => path.endsWith('/batches/async-batch') && options.method === 'GET')).toHaveLength(1);
  expect(fetch.mock.calls.filter(([path, options]) => path.endsWith('/batches') && options.method === 'POST')).toHaveLength(1);
});
it('waits for asynchronous preflight using reads and enables confirmation only when ready', async () => {
  vi.useFakeTimers();
  await open();
  fetch.mockResolvedValueOnce(response({ preview_id: 'preparing-preview', status: 'preparing', items: [], eligible_count: 0, expires_at_ms: 0 }));
  await wrapper.find('[data-test=preview-all]').trigger('click'); await settle();
  expect(wrapper.text()).toContain('正在检查全部目标');
  expect(wrapper.find('[data-test=confirm]').exists()).toBe(false);
  expect(wrapper.find('[data-test=next-page]').attributes('disabled')).toBeDefined();
  fetch.mockResolvedValueOnce(response({ preview_id: 'preparing-preview', status: 'ready', eligible_count: 1, items: [
    { device_id: 'online-A', eligible: true, reason: 'eligible' }
  ], expires_at_ms: Date.now() + 60000 }));
  await vi.advanceTimersByTimeAsync(1000); await settle();
  expect(fetch.mock.calls.filter(([path, options]) => path.endsWith('/batch-previews/preparing-preview') && options.method === 'GET')).toHaveLength(1);
  expect(fetch.mock.calls.filter(([, options]) => options.method === 'POST')).toHaveLength(1);
  expect(wrapper.find('[data-test=confirm]').attributes('disabled')).toBeDefined();
  expect(wrapper.find('[data-test=acknowledge]').exists()).toBe(true);
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
