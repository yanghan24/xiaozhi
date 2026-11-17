import { afterEach, expect, it, vi } from 'vitest';
vi.mock('@/apis/api', () => ({ getServiceUrl: () => '/xiaozhi' }));
import { speakerRequest, balanceText, organizationText, adminToken } from '../src/apis/speaker';
import { speakerRedirect } from '../src/utils/speakerNavigation';

afterEach(() => vi.unstubAllGlobals());
it('separates customer, hardware and administrator entry points in product mode', () => {
  expect(speakerRedirect('/user-management', true).path).toBe('/speaker-users');
  expect(speakerRedirect('/device-management', true).path).toBe('/speaker-devices');
  expect(speakerRedirect('/home', true).path).toBe('/speaker-agents');
  expect(speakerRedirect('/role-config', true)).toBeNull();
  expect(speakerRedirect('/agent-template-management', true)).toEqual({ path: '/speaker-agents', replace: true });
  expect(speakerRedirect('/ota-management', true)).toBeNull();
  expect(speakerRedirect('/product-ota', true)).toEqual({ path: '/speaker-devices', query: { tab: 'firmware' }, replace: true });
  expect(speakerRedirect('/register', true).path).toBe('/login');
  expect(speakerRedirect('/admin-accounts', true)).toBeNull();
  expect(speakerRedirect('/home', false)).toBeNull();
});
it('does not display a failed or missing balance as zero', () => {
  expect(balanceText({ balance_status: 'unavailable', balance: 0 })).toBe('暂时无法查询');
  expect(balanceText({ balance_status: 'not_requested', balance: null })).toBe('尚未查询');
  expect(balanceText({ balance_status: 'available', balance: 0 })).toBe('0 积分');
  expect(balanceText({ balance_status: 'available', balance: -2.5 })).toBe('-2.5 积分');
});
it('renders organization hierarchy as readable names and handles absent group information', () => {
  expect(organizationText({ path: [{ name: '集团' }, { name: '信息部' }] })).toBe('集团 / 信息部');
  expect(organizationText(null)).toBe('集市未提供组织信息');
});
it('uses administrator bearer authentication without browser cookies or caching', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue({ ok: true, json: async () => ({ items: [], total: 0 }) }));
  await speakerRequest('fixture-token', 'customers');
  expect(fetch.mock.calls[0][1]).toMatchObject({ cache: 'no-store', credentials: 'omit', headers: { Authorization: 'Bearer fixture-token' } });
  expect(adminToken({ getters: { getToken: 'broken' } })).toBe('');
});
it('shows administrator and concurrent update errors without upstream response details', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValueOnce({ ok: false, status: 403, json: async () => ({ code: 'secret-internal-detail' }) })
    .mockResolvedValueOnce({ ok: false, status: 409, json: async () => ({ code: 'DEVICE_CHANGED' }) }));
  await expect(speakerRequest('fixture', 'devices')).rejects.toThrow('需要管理员登录或重新登录');
  await expect(speakerRequest('fixture', 'devices/device-a/unbind', {})).rejects.toThrow('设备状态已变化');
});
