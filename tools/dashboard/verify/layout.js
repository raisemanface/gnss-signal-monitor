const { chromium } = require('playwright-core');
const { EDGE, OUT } = require('./_env');

(async () => {
  const browser = await chromium.launch({
    executablePath: EDGE,
    args: ['--autoplay-policy=no-user-gesture-required']
  });
  const page = await browser.newPage({ viewport: { width: 1680, height: 1050 } });
  const errs = [];
  page.on('pageerror', e => errs.push('PAGEERROR ' + e.message));
  await page.goto('http://127.0.0.1:8787/', { waitUntil: 'load' });
  await page.waitForTimeout(5000);

  const rep = await page.evaluate(() => {
    const out = [];
    document.querySelectorAll('.panel[data-p]').forEach(p => {
      const b = p.querySelector('.pbody');
      if (!b) return;
      const ovW = b.scrollWidth - b.clientWidth, ovH = b.scrollHeight - b.clientHeight;
      out.push({ p: p.dataset.p, w: b.clientWidth, h: b.clientHeight, ovW, ovH });
    });
    return out;
  });
  console.log('PANELS', JSON.stringify(rep));
  console.log('ERRS', JSON.stringify(errs));
  await page.screenshot({ path: 'pagefull.png' });
  await browser.close();
})().catch(e => { console.error('FATAL', e); process.exit(1); });
