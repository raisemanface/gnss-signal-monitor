const { chromium } = require('playwright-core');
const { EDGE, OUT } = require('./_env');

(async () => {
  const browser = await chromium.launch({
    executablePath: EDGE,
    args: ['--autoplay-policy=no-user-gesture-required']
  });
  const page = await browser.newPage({ viewport: { width: 1680, height: 1050 } });
  const errs = [];
  page.on('console', m => { if (m.type() === 'error') errs.push('CONSOLE ' + m.text()); });
  page.on('pageerror', e => errs.push('PAGEERROR ' + e.message));

  await page.goto('http://127.0.0.1:8787/', { waitUntil: 'load' });
  await page.waitForTimeout(6000);

  // 08 面板尺寸
  const geom = await page.evaluate(() => {
    const p = document.querySelector('.panel[data-p="08"]');
    const r = p.getBoundingClientRect();
    const b = document.querySelector('.sonbody');
    return { panel: [Math.round(r.width), Math.round(r.height)],
             body: [Math.round(b.getBoundingClientRect().width), Math.round(b.getBoundingClientRect().height)],
             scroll: [b.scrollWidth, b.scrollHeight] };
  });
  console.log('GEOM', JSON.stringify(geom));

  // 打开设置浮层 → 切到合唱
  await page.click('#sonGear');
  await page.waitForTimeout(300);
  await page.evaluate(() => {
    const seg = document.querySelector('#sonCfg .seg[data-k="mode"]');
    const b = Array.from(seg.children).find(x => x.dataset.v === 'chorus');
    b.click();
  });
  await page.waitForTimeout(400);
  const cfgGeom = await page.evaluate(() => {
    const c = document.getElementById('sonCfg');
    return { mode: c.dataset.mode, rect: [Math.round(c.offsetWidth), Math.round(c.offsetHeight)],
             hidden: c.hidden };
  });
  console.log('CFG', JSON.stringify(cfgGeom));

  await page.screenshot({ path: 'cfg.png' });
  await page.evaluate(() => document.getElementById('sonCfgX').click());

  // 点发声，等声部建立
  await page.click('#sonPower');
  await page.waitForTimeout(5000);

  const aud = await page.evaluate(() => {
    const out = { voices: 0, midis: [], levels: [], ctx: null, swing: null, med: null, err: null };
    try {
      const w = window.__SONDBG || {};
      out.voices = (w.CH && w.CH.voices) ? w.CH.voices.size : -1;
      out.med = w.CH ? w.CH.med : null;
    } catch (e) { out.err = String(e); }
    return out;
  });
  console.log('AUD', JSON.stringify(aud));

  const p08 = await page.$('.panel[data-p="08"]');
  await p08.screenshot({ path: 'p08.png' });
  await page.screenshot({ path: 'page.png' });
  console.log('ERRS', JSON.stringify(errs));
  await browser.close();
})().catch(e => { console.error('FATAL', e); process.exit(1); });
