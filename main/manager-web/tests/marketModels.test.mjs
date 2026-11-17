import Vue from 'vue';
import { parseComponent } from 'vue-template-compiler';
import { afterEach, describe, expect, it, vi } from 'vitest';
import source from '../src/components/MarketModelSettings.vue?raw';

const script = parseComponent(source).script.content.replace(/^import .*;$/gm, '').replace('export default', 'return');
const instances = [];
function create(request) {
  const component = new Function('CustomButton', 'adminToken', 'marketModelRequest', script)({}, () => 'admin-fixture', request);
  const vm = new Vue({ ...component, propsData: { modelType: 'llm' } });
  vm.$store = {}; vm.$message = { success: vi.fn(), error: vi.fn() };
  instances.push(vm);
  return vm;
}
async function settle() { await Promise.resolve(); await Promise.resolve(); await Vue.nextTick(); }
afterEach(() => { for (const vm of instances.splice(0)) vm.$destroy(); });

describe('one AI Market adapter configuration', () => {
  it('keeps the saved choice when directory lookup fails and permits disabling it', async () => {
    const request = vi.fn((token, type, action, body) => {
      if (body) return Promise.resolve({});
      if (action === 'options') return Promise.reject(new Error('目录不可用'));
      return Promise.resolve({ publicId: 'saved-model', enabled: true });
    });
    const vm = create(request); await settle();
    expect(vm.selection).toBe('saved-model');
    expect(vm.error).toBe('目录不可用');
    expect(vm.canSave).toBe(false);
    vm.enabled = false;
    expect(vm.canSave).toBe(true);
    await vm.save();
    expect(request).toHaveBeenLastCalledWith('admin-fixture', 'llm', '', { publicId: 'saved-model', enabled: false }, expect.anything());
  });

  it('ignores stale responses when switching model types', async () => {
    const pending = [];
    const vm = create(vi.fn(() => new Promise(resolve => pending.push(resolve))));
    await Vue.nextTick();
    vm.modelType = 'vllm'; await Vue.nextTick();
    pending[2]({ publicId: 'vision-model', enabled: true });
    pending[3]({ items: [{ publicId: 'vision-model', capabilities: ['vision'] }] });
    await settle();
    pending[0]({ publicId: 'old-chat', enabled: true });
    pending[1]({ items: [{ publicId: 'old-chat', capabilities: ['chat'] }] });
    await settle();
    expect(vm.selection).toBe('vision-model');
    expect(vm.options.map(m => m.publicId)).toEqual(['vision-model']);
  });

  it('only labels capabilities that are declared and verified', async () => {
    const vm = create(vi.fn((token,type,action) => Promise.resolve(action === 'options'
      ? { items: [{ publicId: 'checked', capabilities: ['streaming','tools','vision'], verifiedCapabilities: ['streaming','tools'] }] }
      : { publicId: 'checked', enabled: true })));
    await settle();
    expect(vm.capabilityLabels).toEqual(['流式输出','工具调用']);
  });

  it('does not fetch or enable out-of-scope model types' , async () => {
    const request = vi.fn(() => Promise.resolve({ publicId: '', items: [], enabled: false }));
    const vm = create(request); await settle(); request.mockClear();
    vm.modelType = 'tts'; await Vue.nextTick();
    expect(request).not.toHaveBeenCalled();
    expect(vm.supported).toBe(false); expect(vm.canSave).toBe(false);
  });
});
