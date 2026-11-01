const { chromium } = require('playwright-core');
const { EDGE, OUT } = require('./_env');
(async () => {
  const browser = await chromium.launch({
    executablePath: EDGE,
    headless: true, args: ['--window-size=1600,960', '--autoplay-policy=no-user-gesture-required']
  });
  const page = await browser.newPage({ viewport: { width: 1600, height: 960 } });
  const errs = [];
  page.on('pageerror', e => errs.push(e.message));
  await page.goto('http://127.0.0.1:8787/', { waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(3500);

  // 点两颗最强星
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
  const picked = await page.evaluate(() => window.__SONDBG.SON.picked.join(','));

  // 开声，等 10 秒，看敲击发生次数（at 时间戳变化）
  await page.click('#sonPower');
  const st0 = await page.evaluate(() => {
    const o = {}; window.__SONDBG.SV.st.forEach((v, k) => o[k] = v.at); return o;
  });
  await page.waitForTimeout(10000);
  const dbg = await page.evaluate(() => {
    const o = { strikes: 0, states: {}, audio: window.__SONDBG.ctx ? window.__SONDBG.ctx.state : 'none', timbre: window.__SONDBG.SON.timbre };
    window.__SONDBG.SV.st.forEach((v, k) => { o.states[k] = { midi: v.midi, cn: v.cn, at: v.at }; if (v.at > 0) o.strikes++; });
    return o;
  });
  console.log('picked:', picked);
  console.log('audio:', dbg.audio, 'timbre:', dbg.timbre);
  console.log('states:', JSON.stringify(dbg.states));
  console.log('PAGEERRS:', errs.length ? errs : 'none');

  // 切音色试听（应无异常）+ 截 08 面板
  await page.click('#sonGear');
  await page.click('#sonCfg .seg[data-k="timbre"] button[data-v="glass"]');
  await page.waitForTimeout(600);
  const p08 = await page.evaluate(() => {
    const p = document.querySelector('.panel[data-p="08"]');
    const r = p.getBoundingClientRect(); return { x: r.left, y: r.top, width: r.width, height: r.height };
  });
  await page.screenshot({ path: `${OUT}/p08strike.png`, clip: p08 });
  console.log('OK');
  await browser.close();
})().catch(e => { console.error('FATAL', e.message); process.exit(1); });
