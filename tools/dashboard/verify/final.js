const { chromium } = require('playwright-core');
const { EDGE, OUT } = require('./_env');
(async () => {
  const browser = await chromium.launch({
    executablePath: EDGE,
    headless: true, args: ['--window-size=1600,960']
  });
  const page = await browser.newPage({ viewport: { width: 1600, height: 960 } });
  const errs = [];
  page.on('pageerror', e => errs.push(e.message));
  await page.goto('http://127.0.0.1:8787/', { waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(3500);

  // 全面板溢出
  const ov = await page.evaluate(() => {
    const out = [];
    document.querySelectorAll('.panel[data-p]').forEach(p => {
      const b = p.querySelector('.pbody') || p;
      out.push(p.dataset.p + ':' + (b.scrollWidth - b.clientWidth) + '/' + (b.scrollHeight - b.clientHeight));
    });
    return out;
  });
  console.log('panel overflow (x/y):', ov.join(' '));

  // 刷新后 picked 应从 localStorage 恢复
  const before = await page.evaluate(() => window.__SONDBG.SON.picked.length);
  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(2500);
  const after = await page.evaluate(() => window.__SONDBG.SON.picked.length);
  console.log('picked persist: before-reload', before, '→ after-reload', after);
  console.log('PAGEERRS:', errs.length ? errs : 'none');
  await browser.close();
})().catch(e => { console.error('FATAL', e); process.exit(1); });
