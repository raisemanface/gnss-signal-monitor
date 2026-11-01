// v16 点选声化验证：点天球选星 → 声部建立 → 参数浮层 → 北斗字样
const { chromium } = require('playwright-core');
const { EDGE, OUT } = require('./_env');
const fs = require('fs');

(async () => {
  const browser = await chromium.launch({
    executablePath: EDGE,
    headless: true,
    args: ['--autoplay-policy=no-user-gesture-required', '--window-size=1600,960']
  });
  const page = await browser.newPage({ viewport: { width: 1600, height: 960 } });
  const errs = [];
  page.on('pageerror', e => errs.push('PAGEERR: ' + e.message));
  page.on('console', m => { if (m.type() === 'error' && !/favicon/.test(m.text())) errs.push('CONSOLE: ' + m.text()); });

  await page.goto('http://127.0.0.1:8787/', { waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(4000);

  // 从 /api/live 取卫星，按 projD 同款公式算投影坐标
  const sats = await page.evaluate(async () => {
    const r = await fetch('/api/live', { cache: 'no-store' });
    const o = await r.json();
    const list = (o.sky || []).filter(s => s.cn0 > 0 && s.el >= 0);
    return list.map(s => ({ prn: s.prn, az: s.az, el: s.el, snr: s.cn0 }));
  });
  if (!sats.length) { console.log('NO SATS from /api/live'); await browser.close(); return; }
  sats.sort((a, b) => b.snr - a.snr);
  console.log('live sats:', sats.length, 'strongest:', sats[0].prn, sats[0].az + '°', sats[0].el + '°');

  // projD 复刻：cx=W/2, cy=H/2-2, curtW=clamp(W*0.105,56,118), R=min(H*0.405,(W-2curtW-24)/2)
  const geo = await page.evaluate(() => {
    const cv = document.getElementById('sphere');
    const r = cv.getBoundingClientRect();
    return { rectW: r.width, rectH: r.height, left: r.left, top: r.top, backingW: cv.width };
  });
  const W = geo.rectW; // 逻辑宽 ≈ CSS 宽（页面按 1:1 布局时）
  const curtW = Math.max(56, Math.min(118, W * 0.105));
  const H = geo.rectH;
  const R = Math.min(H * 0.405, (W - curtW * 2 - 24) / 2);
  const cx = W / 2, cy = H / 2 - 2;
  const D2R = Math.PI / 180;
  const clickStar = async (s) => {
    const r = R * (1 - Math.min(Math.max(s.el, 0), 90) / 90);
    const lx = cx + r * Math.sin(s.az * D2R);
    const ly = cy - r * Math.cos(s.az * D2R);
    await page.mouse.click(geo.left + lx, geo.top + ly);
  };

  // 1) 点最强星 → picked=1；再点 → 0；再点 → 1
  await clickStar(sats[0]);
  let st = await page.evaluate(() => window.__SONDBG.SON.picked);
  console.log('after click#1 picked =', JSON.stringify(st));
  await clickStar(sats[0]);
  st = await page.evaluate(() => window.__SONDBG.SON.picked);
  console.log('after click#2 (toggle off) picked =', JSON.stringify(st));
  await clickStar(sats[0]);

  // 2) 点第二强星 → picked=2；开声 → voices=2
  if (sats[1]) await clickStar(sats[1]);
  st = await page.evaluate(() => window.__SONDBG.SON.picked);
  console.log('picked now =', JSON.stringify(st));
  await page.click('#sonPower');
  await page.waitForTimeout(2500);
  const aud = await page.evaluate(() => {
    const d = window.__SONDBG;
    return { power: d.SON.power, voices: d.SV.voices.size, active: [...d.SV.voices.values()].filter(v => v.active).length,
             ctx: d.ctx ? d.ctx.state : 'none', hist: d.SV.hist.size };
  });
  console.log('audio state:', JSON.stringify(aud));

  // 3) 浮层内容：应只有 音阶 seg + 音量滑杆
  await page.click('#sonGear');
  await page.waitForTimeout(300);
  const cfg = await page.evaluate(() => {
    const p = document.getElementById('sonCfg');
    return {
      segs: [...p.querySelectorAll('.seg')].map(s => s.dataset.k),
      sliders: [...p.querySelectorAll('input[type=range]')].map(i => i.id),
      hidden: p.hidden
    };
  });
  console.log('overlay:', JSON.stringify(cfg));

  // 4) 北斗字样检查（界面上不应再有"北斗"）
  const bd = await page.evaluate(() => (document.body.innerText.match(/北斗/g) || []).length);
  console.log('"北斗" occurrences in visible text:', bd);

  // 5) 关声 → 声部清零
  await page.click('#sonPower');
  await page.waitForTimeout(400);
  const off = await page.evaluate(() => ({ power: window.__SONDBG.SON.power, voices: window.__SONDBG.SV.voices.size }));
  console.log('after power off:', JSON.stringify(off));

  // 6) 08 面板溢出检查
  const ov = await page.evaluate(() => {
    const p = document.querySelector('.panel[data-p="08"]');
    if (!p) return 'no p08';
    const b = p.querySelector('.pbody') || p;
    return { sw: b.scrollWidth - b.clientWidth, sh: b.scrollHeight - b.clientHeight };
  });
  console.log('p08 overflow:', JSON.stringify(ov));

  console.log('ERRORS:', errs.length ? errs.slice(0, 6) : 'none');
  await page.screenshot({ path: `${OUT}/v16.png`, clip: { x: 0, y: 0, width: 800, height: 960 } });
  await browser.close();
})().catch(e => { console.error('FATAL', e); process.exit(1); });
