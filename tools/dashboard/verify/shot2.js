const { chromium } = require('playwright-core');
const { EDGE, OUT } = require('./_env');
(async () => {
  const browser = await chromium.launch({
    executablePath: EDGE,
    headless: true, args: ['--window-size=1600,960']
  });
  const page = await browser.newPage({ viewport: { width: 1600, height: 960 } });
  const fails = [];
  page.on('response', r => { if (r.status() === 404) fails.push(r.url()); });
  page.on('pageerror', e => console.log('PAGEERR:', e.message));
  await page.goto('http://127.0.0.1:8787/', { waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(3500);

  const sats = await page.evaluate(async () => {
    const o = await (await fetch('/api/live')).json();
    return (o.sky || []).filter(s => s.cn0 > 0 && s.el >= 0).sort((a, b) => b.cn0 - a.cn0).slice(0, 3)
      .map(s => ({ prn: s.prn, az: s.az, el: s.el }));
  });
  const geo = await page.evaluate(() => {
    const r = document.getElementById('sphere').getBoundingClientRect();
    return { w: r.width, h: r.height, left: r.left, top: r.top };
  });
  const curtW = Math.max(56, Math.min(118, geo.w * 0.105));
  const R = Math.min(geo.h * 0.405, (geo.w - curtW * 2 - 24) / 2);
  const cx = geo.w / 2, cy = geo.h / 2 - 2, D2R = Math.PI / 180;
  for (const s of sats) {
    const r = R * (1 - Math.min(s.el, 90) / 90);
    await page.mouse.click(geo.left + cx + r * Math.sin(s.az * D2R), geo.top + cy - r * Math.cos(s.az * D2R));
    await page.waitForTimeout(150);
  }
  await page.click('#sonPower');
  await page.waitForTimeout(6000);
  const dbg = await page.evaluate(() => ({ picked: window.__SONDBG.SON.picked, voices: window.__SONDBG.SV.voices.size }));
  console.log('picked/voices:', JSON.stringify(dbg));

  // 天球区域截图（光环应可见）
  const dome = await page.evaluate(() => {
    const r = document.getElementById('sphere').getBoundingClientRect();
    return { x: r.left, y: r.top, width: r.width, height: r.height };
  });
  await page.screenshot({ path: `${OUT}/dome.png`, clip: dome });
  // 08 面板截图
  const p08 = await page.evaluate(() => {
    const p = document.querySelector('.panel[data-p="08"]');
    const r = p.getBoundingClientRect();
    return { x: r.left, y: r.top, width: r.width, height: r.height };
  });
  await page.screenshot({ path: `${OUT}/p08.png`, clip: p08 });
  console.log('404s:', JSON.stringify(fails));
  await browser.close();
})().catch(e => { console.error('FATAL', e); process.exit(1); });
