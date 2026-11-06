import assert from 'node:assert/strict';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve, dirname } from 'node:path';
import { createInterface } from 'node:readline/promises';
import { parseArgs } from 'node:util';
import { chromium } from 'playwright-core';

const { values } = parseArgs({ options: {
  credentials: { type: 'string' },
  output: { type: 'string', default: '.runtime/voice/admin-token.json' },
  url: { type: 'string', default: 'http://127.0.0.1:18002' },
} });
assert.ok(values.credentials, 'Pass --credentials pointing to the existing private test account file');
const credentials = JSON.parse(await readFile(values.credentials, 'utf8'));
const output = resolve(values.output);
await mkdir(dirname(output), { recursive: true, mode: 0o700 });
const browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/usr/bin/google-chrome', headless: true });
const terminal = createInterface({ input: process.stdin, output: process.stdout });
const page = await browser.newPage({ locale: 'zh-CN' });
try {
  page.on('pageerror', error => console.error(`Browser error: ${error.message}`));
  page.setDefaultTimeout(30000);
  await page.goto(`${values.url}/#/login`);
  await page.getByPlaceholder('请输入用户名', { exact: true }).fill(credentials.username);
  await page.getByPlaceholder('请输入密码', { exact: true }).fill(credentials.password);
  const image = resolve(dirname(output), 'captcha.png');
  await page.locator('img[alt="验证码"]:visible').screenshot({ path: image });
  console.log(`Captcha image: ${image}`);
  const answer = await terminal.question('Enter captcha (5 characters): ', { signal: AbortSignal.timeout(600000) });
  assert.match(answer.trim(), /^[A-Za-z0-9]{5}$/);
  await page.getByPlaceholder('请输入验证码', { exact: true }).fill(answer.trim());
  const pending = page.waitForResponse(response => response.url().endsWith('/user/login') && response.request().method() === 'POST');
  await page.locator('.login-btn').click();
  const result = await (await pending).json();
  assert.equal(result.code, 0, 'Real management login failed');
  assert.ok(result.data?.token, 'Login did not return a token');
  await writeFile(output, JSON.stringify({ token: result.data.token }), { mode: 0o600 });
  console.log('PASS: real management login; token saved privately');
} catch (error) {
  await page.screenshot({ path: resolve(dirname(output), 'login-failure.png') }).catch(() => {});
  console.error(`Login page: ${page.url()}`);
  throw error;
} finally {
  terminal.close();
  await browser.close();
}
