const { chromium } = require('playwright-core');
const { EDGE, OUT } = require('./_env');
(async () => {
  const browser = await chromium.launch({
    executablePath: EDGE,
    headless: true, args: ['--window-size=1600,960']
  });
  const ctx = await browser.newContext({ viewport: { width: 1600, height: 960 } });
  const page = await ctx.newPage();
  await page.goto('http://127.0.0.1:8787/', { waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(3000);
  const sats = await page.evaluate(async () => {
    const o = await (await fetch('/api/live')).json();
    return (o.sky || []).filter(s => s.cn0 > 0 && s.el >= 0).sort((a, b) => b.cn0 - a.cn0).slice(0, 2);
  });
  const geo = await page.evaluate(() => {
    const r = document.getElementById('sphere').getBoundingClientRect();
    return { w: r.width, h: r.height, left: r.left, top: r.top };
  });
  const curtW = Math.max(56, Math.min(118, geo.w * 0.105));
  const R = Math.min(geo.h * 0.405, (geo.w - curtW * 2 - 24) / 2);
  for (const s of sats) {
    const r = R * (1 - Math.min(s.el, 90) / 90);
    await page.mouse.click(geo.left + geo.w / 2 + r * Math.sin(s.az * Math.PI / 180),
                           geo.top + geo.h / 2 - 2 - r * Math.cos(s.az * Math.PI / 180));
    await page.waitForTimeout(120);
  }
  const n1 = await page.evaluate(() => window.__SONDBG.SON.picked.length);
  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(2500);
  const n2 = await page.evaluate(() => window.__SONDBG.SON.picked.length);
  const names = await page.evaluate(() => window.__SONDBG.SON.picked.join(','));
  console.log('picked after click:', n1, '→ after reload:', n2, '(', names, ')');
  await browser.close();
})().catch(e => { console.error('FATAL', e); process.exit(1); });
