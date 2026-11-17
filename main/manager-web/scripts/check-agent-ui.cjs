// Run only through deploy/local-zhou/check-agent-management.py inside xiaozhi-dev.
const fs = require('fs');
const { chromium } = require('/opt/xiaozhi-dev/browser-tools/node_modules/playwright');
const fixture = JSON.parse(fs.readFileSync('/opt/xiaozhi-dev/run/agent-ui-fixture.json', 'utf8'));
(async () => {
  const browser = await chromium.launch({ headless: true });
  const page = await browser.newPage({ viewport: { width: 1440, height: 1050 }, locale:'zh-CN' });
  const errors = [];
  page.on('pageerror', e => errors.push(e.message));
  await page.addInitScript(data => {
    localStorage.setItem('token', JSON.stringify({token:data.token,expire:7200,_localStoredAt:Date.now()}));
    localStorage.setItem('userInfo', JSON.stringify(data.user));
    localStorage.setItem('pubConfig', JSON.stringify(data.public));
    localStorage.setItem('userLanguage', 'zh_CN');
  }, fixture);
  // Scope this check to the local UI/API; no remote previews or providers.
  await page.route('**/*', route => new URL(route.request().url()).hostname === '127.0.0.1' ? route.continue() : route.abort());
  try {
    await page.goto('http://127.0.0.1:8001/#/speaker-agents');
    await page.locator('.device-item').filter({hasText:fixture.name}).waitFor();
    await page.getByText('当前默认', { exact: true }).first().waitFor();
    await page.screenshot({ path:'/opt/xiaozhi-dev/run/agent-cards.png', fullPage:true, animations:'disabled' });
    await page.locator('.device-item').filter({hasText:fixture.name}).getByText('配置角色', {exact:true}).click();
    await page.waitForURL('**/#/role-config?agentId=*');
    await page.locator('.config-header').getByText(fixture.name, {exact:true}).waitFor();
    await page.getByText('湾湾小何',{exact:true}).waitFor();
    await page.getByText(fixture.mcpName+'（已停用）',{exact:true}).first().waitFor();
    if(await page.locator('.el-select').count()<5) throw Error('Original model/voice selectors were not restored');
    await page.getByRole('button',{name:'语调发音替换词',exact:true}).click();
    await page.locator('.el-drawer:visible').waitFor();
    await page.screenshot({ path:'/opt/xiaozhi-dev/run/agent-voice-settings.png', fullPage:true, animations:'disabled' });
    await page.locator('.el-drawer:visible .drawer-close-btn').click();
    await page.getByRole('button',{name:'历史版本',exact:true}).click();
    await page.locator('.agent-snapshot-dialog .el-dialog:visible').waitFor();
    await page.screenshot({ path:'/opt/xiaozhi-dev/run/agent-history.png', fullPage:true, animations:'disabled' });
    await page.locator('.agent-snapshot-dialog .el-dialog__headerbtn').click();
    await page.locator('.agent-snapshot-dialog .el-dialog').waitFor({state:'hidden'});
    await page.screenshot({ path:'/opt/xiaozhi-dev/run/agent-editor.png', fullPage:true, animations:'disabled' });
    await page.goto('http://127.0.0.1:8001/#/agent-template-management');
    await page.getByText('星际游子',{exact:true}).waitFor();
    await page.getByText('汪汪队长',{exact:true}).waitFor();
    await page.goto('http://127.0.0.1:8001/#/mcp-management');
    await page.getByText(fixture.mcpName,{exact:true}).waitFor();
    await page.screenshot({path:'/opt/xiaozhi-dev/run/restoration-mcp.png',fullPage:true});
    await page.goto('http://127.0.0.1:8001/#/speaker-devices?tab=firmware');
    await page.getByRole('tab',{name:'固件与 OTA',exact:true}).waitFor();
    if (await page.getByRole('tab').count()!==2) throw Error('Duplicate firmware catalogues remain');
    await page.screenshot({path:'/opt/xiaozhi-dev/run/restoration-ota.png',fullPage:true});
    if(errors.length) throw Error('Browser runtime errors: '+errors.join(';'));
    console.log('Agent card, complete editor, voice settings, history and original templates verified');
  } finally { await browser.close(); }
})().catch(e=>{ console.error(e.message); process.exitCode=1; });
