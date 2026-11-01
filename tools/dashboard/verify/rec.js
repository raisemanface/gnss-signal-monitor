// GNSS 看板录制：CDP 抓帧 + 页面内 MediaRecorder 录音
const { chromium } = require('playwright-core');
const fs = require('fs');
const path = require('path');

const DUR = Number(process.argv[2] || 6);          // 录制秒数
const PRE = Number(process.argv[4] || 3);          // 预热秒数（等星轨低通/声部建立）
const OUT = process.argv[3] || path.join(__dirname, 'out');
const W = 1600, H = 960;

(async () => {
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(OUT, { recursive: true });

  const browser = await chromium.launch({
    channel: 'msedge',
    headless: false,
    args: [
      '--autoplay-policy=no-user-gesture-required',
      '--disable-infobars', '--no-first-run',
      `--window-size=${W},${H}`, '--window-position=0,0',
      '--force-device-scale-factor=1',
    ],
  });
  const ctx = await browser.newContext({ viewport: { width: W, height: H }, deviceScaleFactor: 1 });
  const page = await ctx.newPage();
  page.on('console', m => { if (m.type() === 'error') console.log('[page:err]', m.text()); });

  await page.goto('http://127.0.0.1:8787/', { waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(1500);

  // 存档切到合唱
  await page.evaluate(() => {
    let o = {};
    try { o = JSON.parse(localStorage.getItem('gnss.sonify.v1') || '{}') || {}; } catch (e) {}
    o.mode = 'chorus'; o.cap = 14; o.dbmap = 'dev';
    localStorage.setItem('gnss.sonify.v1', JSON.stringify(o));
  });
  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(1200);

  // 开启发声
  await page.click('#sonPower');
  await page.waitForTimeout(PRE * 1000);

  const state = await page.evaluate(() => ({
    mode: window.__SONDBG.SON.mode,
    ctx: !!window.__SONDBG.ctx,
    ctxState: window.__SONDBG.ctx && window.__SONDBG.ctx.state,
    master: !!window.__SONDBG.master,
    voices: window.__SONDBG.CH ? window.__SONDBG.CH.voices.size : -1,
  }));
  console.log('STATE', JSON.stringify(state));
  if (!state.master) { console.log('FATAL: 拿不到音频母线'); await browser.close(); process.exit(1); }

  // 启动抓帧
  const cdp = await ctx.newCDPSession(page);
  const frames = [];
  let t0v = 0;
  cdp.on('Page.screencastFrame', async f => {
    if (!t0v) t0v = Date.now();
    frames.push({ ts: f.metadata.timestamp, t: Date.now() });
    fs.writeFileSync(path.join(OUT, `f_${String(frames.length).padStart(5, '0')}.jpg`),
      Buffer.from(f.data, 'base64'));
    cdp.send('Page.screencastFrameAck', { sessionId: f.sessionId }).catch(() => {});
  });
  await cdp.send('Page.startScreencast', {
    format: 'jpeg', quality: 94, maxWidth: W, maxHeight: H, everyNthFrame: 1,
  });

  // 启动录音
  const t0a = await page.evaluate(() => {
    const d = window.__SONDBG, c = d.ctx, m = d.master;
    const dest = c.createMediaStreamDestination();
    m.connect(dest);
    const rec = new MediaRecorder(dest.stream, { mimeType: 'audio/webm;codecs=opus', audioBitsPerSecond: 192000 });
    const R = { rec, chunks: [], t0: Date.now() };
    window.__REC = R;
    rec.ondataavailable = e => { if (e.data && e.data.size) R.chunks.push(e.data); };
    rec.start(250);
    return R.t0;
  });

  await page.waitForTimeout(DUR * 1000);
  await cdp.send('Page.stopScreencast').catch(() => {});

  const rec = await page.evaluate(async () => {
    const R = window.__REC;
    const done = new Promise(res => { R.rec.onstop = () => res(); });
    R.rec.stop();
    await done;
    const blob = new Blob(R.chunks, { type: 'audio/webm' });
    const buf = new Uint8Array(await blob.arrayBuffer());
    let s = ''; const CHK = 0x8000;
    for (let i = 0; i < buf.length; i += CHK) s += String.fromCharCode.apply(null, buf.subarray(i, i + CHK));
    return { b64: btoa(s), bytes: buf.length, t0: R.t0 };
  });

  fs.writeFileSync(path.join(OUT, 'audio.webm'), Buffer.from(rec.b64, 'base64'));
  fs.writeFileSync(path.join(OUT, 'frames.json'), JSON.stringify({ t0a, t0v, frames }, null, 0));

  const last = frames.length ? frames[frames.length - 1].t : t0v;
  console.log('REC', JSON.stringify({
    frames: frames.length,
    fps: +(frames.length / Math.max(0.001, (last - t0v) / 1000)).toFixed(1),
    videoSpan: last - t0v,
    audioBytes: rec.bytes,
    audioT0: t0a, videoT0: t0v, skew: t0a - t0v,
    voices: (await page.evaluate(() => window.__SONDBG.CH.voices.size)),
  }));

  await browser.close();
})().catch(e => { console.error('ERR', e.message); process.exit(1); });
