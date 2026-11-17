import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { createInterface } from 'node:readline/promises';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';

// This check uses the real browser form and the real captcha. Enter the image's
// characters at each prompt; no auth endpoints, caches, or responses are mocked.
const root = fileURLToPath(new URL('../../', import.meta.url));
const output = resolve(root, '.runtime/manager-check');
await mkdir(output, { recursive: true, mode: 0o700 });
const base = process.env.MANAGER_URL || 'http://127.0.0.1:18002';
const resultPath = resolve(output, 'result.json');
await writeFile(resultPath, JSON.stringify({ at: new Date().toISOString(), base, result: 'running' }));
const register = process.argv.includes('--register');
const credentialsPath = resolve(output, 'credentials.json');
let credentials;
try {
  credentials = JSON.parse(await readFile(credentialsPath, 'utf8'));
} catch (error) {
  if (error.code !== 'ENOENT' || !register) throw error;
  credentials = { username: 'baseline_admin', password: `Aa1!${randomBytes(8).toString('hex')}` };
  await writeFile(credentialsPath, JSON.stringify(credentials), { mode: 0o600, flag: 'wx' });
}
const terminal = createInterface({ input: process.stdin, output: process.stdout });
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/bin/google-chrome',
  headless: true,
});
const page = await browser.newPage({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN' });
page.setDefaultTimeout(30000);
const observations = [];
function pass(check, details = {}) {
  observations.push({ check, result: 'pass', ...details });
  console.log(`PASS: ${check}`);
}

async function captcha() {
  const picture = page.locator('img[alt="验证码"]:visible');
  await picture.waitFor();
  await picture.screenshot({ path: resolve(output, 'captcha.png') });
  console.log(`Captcha image: ${resolve(output, 'captcha.png')}`);
  const answer = await terminal.question('Enter captcha (5 characters): ', { signal: AbortSignal.timeout(600000) });
  assert.match(answer.trim(), /^[A-Za-z0-9]{5}$/);
  await page.getByPlaceholder('请输入验证码', { exact: true }).fill(answer.trim());
}

async function submitAuth(endpoint, expectedCode) {
  for (let attempt = 0; attempt < 3; attempt++) {
    await captcha();
    const responsePromise = page.waitForResponse(response => response.url().endsWith(endpoint) && response.request().method() === 'POST');
    await page.locator('.login-btn').click();
    const response = await responsePromise;
    const result = await response.json();
    if (result.code === 10067 && attempt < 2) {
      console.log('Captcha was rejected; read the refreshed image and retry.');
      // The upstream form refreshes its captcha on a delayed timer after submit.
      // Reload cancels that timer so it cannot invalidate the next displayed image.
      const fields = await page.locator('input[placeholder]').evaluateAll(inputs =>
        inputs.map(input => ({ placeholder: input.placeholder, value: input.value })));
      await page.reload();
      for (const field of fields) {
        if (field.placeholder !== '请输入验证码') {
          await page.getByPlaceholder(field.placeholder, { exact: true }).fill(field.value);
        }
      }
      continue;
    }
    assert.equal(result.code, expectedCode, `Unexpected response code for ${endpoint}`);
    return result;
  }
}

try {
  if (register) {
    await page.goto(`${base}/#/register`);
    await page.getByPlaceholder('请输入用户名', { exact: true }).fill(credentials.username);
    await page.getByPlaceholder('请设置密码', { exact: true }).fill(credentials.password);
    await page.getByPlaceholder('请确认密码', { exact: true }).fill(credentials.password);
    await submitAuth('/user/register', 0);
    pass('first administrator registered through the browser');
  }

  await page.goto(`${base}/#/login`);
  await page.getByPlaceholder('请输入用户名', { exact: true }).fill(credentials.username);
  await page.getByPlaceholder('请输入密码', { exact: true }).fill('intentionally-wrong-password');
  const rejected = await submitAuth('/user/login', 10004);
  assert.ok(!rejected.data?.token, 'rejected login must not return a token');
  await page.getByPlaceholder('请输入密码', { exact: true }).waitFor();
  pass('valid captcha with wrong password rejected', { code: 10004 });

  // A fresh page also ensures the preceding failure did not establish a session.
  await page.reload();
  await page.getByPlaceholder('请输入用户名', { exact: true }).fill(credentials.username);
  await page.getByPlaceholder('请输入密码', { exact: true }).fill(credentials.password);
  await submitAuth('/user/login', 0);
  await page.waitForURL('**/#/home');
  pass('administrator logged in through the browser');
  // Reload the authenticated page before navigating: the upstream login form's
  // delayed captcha callback otherwise redirects an immediate hash navigation.
  await page.reload();

  await page.goto(`${base}/#/params-management`);
  const marker = `baseline.acceptance.${Date.now()}`;
  const value = 'manager-write-read-verified';
  await page.getByText('新增', { exact: true }).click();
  await page.getByPlaceholder('请输入参数编码', { exact: true }).fill(marker);
  await page.getByPlaceholder('请输入参数值', { exact: true }).fill(value);
  const savePromise = page.waitForResponse(response => response.url().endsWith('/admin/params') && response.request().method() === 'POST');
  await page.getByText('确定', { exact: true }).click();
  assert.equal((await (await savePromise).json()).code, 0);
  pass('test configuration saved through the browser', { marker, value });

  await page.reload();
  await page.getByPlaceholder('请输入参数编码或备注查询', { exact: true }).fill(marker);
  await page.getByText('搜索', { exact: true }).click();
  const row = page.locator('.el-table__body-wrapper tr').filter({ hasText: marker });
  await row.waitFor();
  assert.ok((await row.innerText()).includes(value), 'reloaded configuration must match the saved value');
  await page.screenshot({ path: resolve(output, 'configuration-readback.png'), fullPage: true });
  pass('configuration read back after full page reload', { marker, value });
  await writeFile(resultPath, JSON.stringify({ at: new Date().toISOString(), base, result: 'pass', observations }, null, 2));
  console.log(`Evidence: ${resultPath}`);
} catch (error) {
  console.error(`Page: ${page.url()}`);
  await page.screenshot({ path: resolve(output, 'failure.png'), fullPage: true }).catch(() => {});
  await writeFile(resultPath, JSON.stringify({ at: new Date().toISOString(), base, result: 'fail', observations, error: error.message }, null, 2));
  console.error(`FAIL: ${error.message}`);
  process.exitCode = 1;
} finally {
  terminal.close();
  await browser.close();
}
