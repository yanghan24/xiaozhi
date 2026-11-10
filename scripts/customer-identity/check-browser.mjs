import assert from 'node:assert/strict';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { chromium } from 'playwright-core';

const fixture = JSON.parse(await readFile(process.env.FIXTURE_FILE, 'utf8'));
const output = process.env.EVIDENCE_DIR;
await mkdir(output, { recursive: true });
const args = ['--no-sandbox'];
if (process.env.BROWSER_PROXY_IP) args.push(`--host-resolver-rules=MAP bridge.hncig.cn ${process.env.BROWSER_PROXY_IP}`);
const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROME_PATH, args });
// This is an isolated generated certificate. Production certificate checks belong to #11.
const context = await browser.newContext({ ignoreHTTPSErrors: true, viewport: { width: 390, height: 844 }, locale: 'zh-CN' });
const base = 'https://bridge.hncig.cn:18443';
const apiBase = process.env.BROWSER_PROXY_IP ? `https://${process.env.BROWSER_PROXY_IP}:18443` : base;
const apiHeaders = { Host: 'bridge.hncig.cn:18443' };
const observations = [];
try {
  const page = await context.newPage();
  page.on('request', request => {
    if (request.method() === 'POST' && new URL(request.url()).pathname === '/xiaozhi/customer/v1/binding-flows') {
      console.log(JSON.stringify({ formOrigin: request.headers().origin ?? 'absent' }));
    }
  });
  page.on('console', message => {
    if (message.type() === 'error') console.error(message.text().replace(/\?[^\s'"<>]*/g, '?[redacted]'));
  });
  await page.goto(`${base}/xiaozhi/bind/${fixture.handles[0]}`);
  await page.getByRole('button', { name: '通过 AI 集市确认身份' }).click();
  await page.getByRole('button', { name: '允许', exact: true }).click();
  await page.getByRole('heading', { name: '身份已确认，等待手机确认' }).waitFor();
  assert.match(page.url(), /^https:\/\/bridge\.hncig\.cn:18443\/xiaozhi\/customer\/results\/[A-Za-z0-9_-]{43}$/);
  const firstResult = page.url();
  const flowId = firstResult.split('/').at(-1);
  const statusPage = await context.newPage();
  const versioned = await statusPage.goto(`${base}/xiaozhi/customer/v1/binding-flows/${flowId}`);
  assert.equal(versioned.status(), 200);
  const versionedBody = await versioned.json();
  await statusPage.close();
  assert.equal(versionedBody.version, 1);
  assert.equal(versionedBody.code, 'OK');
  assert.equal(versionedBody.data.status, 'IDENTITY_VERIFIED');
  await page.screenshot({ path: `${output}/identity-confirmed.png`, fullPage: true });
  observations.push('phone consent, successful callback, clean URL');

  const other = await context.newPage();
  await other.goto(`${base}/xiaozhi/bind/${fixture.handles[1]}`);
  await other.getByRole('button', { name: '通过 AI 集市确认身份' }).click();
  await other.getByRole('button', { name: '拒绝', exact: true }).click();
  await other.getByRole('heading', { name: '已取消身份授权，请重新扫码' }).waitFor();
  await page.reload();
  assert.equal(page.url(), firstResult);
  await page.getByRole('heading', { name: '身份已确认，等待手机确认' }).waitFor();
  observations.push('refusal and same-browser tab isolation');

  await page.getByRole('button', { name: '确认绑定设备', exact: true }).click();
  await page.getByRole('heading', { name: '绑定成功', exact: true }).waitFor();
  assert.equal(page.url(), firstResult);
  await page.screenshot({ path: `${output}/binding-confirmed.png`, fullPage: true });
  await page.reload();
  await page.getByRole('heading', { name: '绑定成功', exact: true }).waitFor();
  assert.equal(await page.getByRole('button', { name: '确认绑定设备', exact: true }).count(), 0);
  observations.push('explicit phone confirmation commits binding; reload keeps success without a second mutation');

  await page.getByRole('link', { name: '账号与绑定', exact: true }).click();
  await page.getByRole('heading', { name: '账号与绑定', exact: true }).waitFor();
  await page.getByRole('button', { name: '退出本台音箱', exact: true }).click();
  await page.getByText('退出已保存，等待音箱确认停播', { exact: true }).waitFor();
  await page.screenshot({ path: `${output}/device-signed-out.png`, fullPage: true });
  await page.reload();
  await page.getByText('退出已保存，等待音箱确认停播', { exact: true }).waitFor();
  assert.equal(await page.getByRole('button', { name: '退出本台音箱', exact: true }).count(), 0);
  observations.push('owner device page, explicit single-device sign-out, truthful pending-ACK status and read-only reload');

  const device = fixture.devices[0];
  const deviceHeaders = { ...apiHeaders, 'Device-Id': device.id, Authorization: `Bearer ${device.credential}` };
  const state = async () => (await (await context.request.get(`${apiBase}/xiaozhi/device-control/v1/state`, { headers: deviceHeaders })).json()).data;
  const stopped = await state();
  assert.equal(stopped.reason, 'user_signed_out');
  const request = await context.request.post(`${apiBase}/xiaozhi/device-control/v1/requests`, {
    headers: { ...deviceHeaders, 'Idempotency-Key': 'browser-resume' },
    data: { intent: 'auto', expected_permission_version: stopped.permission_version }
  });
  assert.equal(request.status(), 200);
  await page.goto((await request.json()).data.binding_request.verification_uri);
  await page.getByRole('button', { name: '通过 AI 集市确认身份' }).click();
  await page.getByRole('button', { name: '允许', exact: true }).click();
  await page.getByRole('button', { name: '恢复本台使用', exact: true }).waitFor();
  await page.reload();
  assert.equal((await state()).reason, 'user_signed_out');
  await page.getByRole('button', { name: '恢复本台使用', exact: true }).click();
  await page.getByRole('heading', { name: '恢复操作已完成', exact: true }).waitFor();
  assert.equal((await state()).use_permission, 'allowed');
  await page.reload();
  assert.equal(await page.getByRole('button', { name: '恢复本台使用', exact: true }).count(), 0);
  await page.screenshot({ path: `${output}/device-resumed.png`, fullPage: true });
  observations.push('owner login and reload do not resume; explicit phone resume restores target permission');

  for (const path of ['/xiaozhi/user/login', '/xiaozhi/user/register', '/xiaozhi/config/agent', '/xiaozhi/customer/unknown', '/xiaozhi/customer/v1/unknown', '/xiaozhi/internal/device-voice/v1/model-access', '/xiaozhi/device-control/v1/unknown']) {
    const response = await context.request.get(apiBase + path, { headers: apiHeaders });
    assert.equal(response.status(), 404);
  }
  assert.equal((await context.request.post(`${apiBase}/xiaozhi/customer/binding-flows`, { headers: apiHeaders, data: { request_handle: fixture.handles[2] } })).status(), 403);
  assert.equal((await context.request.post(`${apiBase}/xiaozhi/customer/oidc/backchannel-logout`, { headers: apiHeaders, form: { logout_token: 'invalid' } })).status(), 400);
  observations.push('proxy denies admin and unknown routes; CSRF and signed notification boundary');
  const content = await page.content();
  assert.ok(!content.includes('test-business-key') && !content.includes('test-only-secret') && !content.includes('access_token'));
  const cookie = (await context.cookies(`${base}/xiaozhi/`)).find(c => c.name === 'customer_browser');
  assert.ok(cookie.secure && cookie.httpOnly && cookie.sameSite === 'Lax');
  observations.push('secure HttpOnly cookie; no tokens or Key in phone result');
  await writeFile(`${output}/browser-result.json`, JSON.stringify({ result: 'pass', externalMarket: 'controlled substitute', observations }, null, 2));
  console.log(JSON.stringify({ result: 'pass', observations }));
} finally {
  await browser.close();
}
