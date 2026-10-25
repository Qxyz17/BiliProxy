'use strict';

/**
 * Probe: load a Bilibili video page as a guest in headless Edge and record
 * DOM nodes that look like login popups, to learn their real selectors.
 *
 * Usage: node tools/probe-login-popup.js [seconds] [url]
 */

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9337;
const SECONDS = Number(process.argv[2] || 180);
const URL_ = process.argv[3] || 'https://www.bilibili.com/video/BV1nFzdYfEgs/';
const OUT = path.join(__dirname, '..', '_probe');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

let ws;
let seq = 0;
const pending = new Map();

function send(method, params = {}) {
  const id = ++seq;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    setTimeout(() => {
      if (pending.has(id)) { pending.delete(id); reject(new Error('CDP timeout: ' + method)); }
    }, 30000);
  });
}

async function evaluate(expression) {
  const r = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  if (r.exceptionDetails) throw new Error('eval: ' + (r.exceptionDetails.exception?.description || r.exceptionDetails.text));
  return r.result.value;
}

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const profile = path.join(process.env.TEMP, 'edge-bili-probe');
  fs.rmSync(profile, { recursive: true, force: true });
  spawn(EDGE, [
    '--headless=new', '--disable-gpu', '--no-first-run', '--mute-audio',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    '--disable-backgrounding-occluded-windows',
    `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
    '--window-size=1280,900', URL_,
  ], { stdio: 'ignore' });

  let target;
  for (let i = 0; i < 80; i++) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
      target = list.find((t) => t.type === 'page' && t.url.includes('bilibili.com'));
      if (target) break;
    } catch {}
    await sleep(250);
  }
  if (!target) throw new Error('Edge CDP not reachable');

  ws = new WebSocket(target.webSocketDebuggerUrl);
  ws.addEventListener('message', async (ev) => {
    let raw = ev.data;
    if (typeof raw !== 'string') raw = raw instanceof Blob ? await raw.text() : Buffer.from(raw).toString('utf8');
    const m = JSON.parse(raw);
    if (m.id && pending.has(m.id)) {
      const { resolve, reject } = pending.get(m.id);
      pending.delete(m.id);
      if (m.error) reject(new Error(m.error.message)); else resolve(m.result);
    }
  });
  await new Promise((res, rej) => { ws.addEventListener('open', res); ws.addEventListener('error', rej); });

  await send('Page.enable');
  await sleep(3000);

  await evaluate(`
    window.__hits = [];
    const RE = /login|signin|sign-in|dialog|popup|modal|mask|弹窗|登录/i;
    const rec = (n, why) => {
      if (!n || n.nodeType !== 1) return;
      const cls = (typeof n.className === 'string' ? n.className : (n.className && n.className.baseVal) || '');
      const id = n.id || '';
      const style = (n.getAttribute && n.getAttribute('style')) || '';
      if (!(RE.test(cls) || RE.test(id) || RE.test(style))) return;
      window.__hits.push({
        t: Math.round(performance.now() / 1000),
        why, tag: n.tagName, id, cls: cls.slice(0, 160),
        style: style.slice(0, 120),
        text: (n.innerText || '').replace(/\\s+/g, ' ').slice(0, 80),
      });
      if (window.__hits.length > 300) window.__hits.shift();
    };
    new MutationObserver((ms) => {
      for (const m of ms) for (const n of m.addedNodes) rec(n, 'added');
    }).observe(document.documentElement, { childList: true, subtree: true });
    true;
  `);

  const t0 = Date.now();
  const samples = [];
  for (let i = 0; i < SECONDS; i += 15) {
    await sleep(15000);
    const info = await evaluate(`(() => {
      const dlg = [...document.querySelectorAll('[class*=login],[id*=login],[class*=Login]')].slice(0, 12)
        .map(e => ({ tag: e.tagName, id: e.id, cls: (typeof e.className === 'string' ? e.className : '').slice(0,120),
                     vis: e.offsetParent !== null, z: getComputedStyle(e).zIndex }));
      return { title: document.title, dlg, hits: window.__hits.length };
    })()`).catch(() => null);
    samples.push({ at: Math.round((Date.now() - t0) / 1000), info });
  }

  const hits = await evaluate('window.__hits');
  const final = await evaluate(`(() => {
    const all = [...document.querySelectorAll('body *')].filter(e => {
      const cs = getComputedStyle(e);
      return cs.position === 'fixed' && Number(cs.zIndex) > 100 && e.offsetParent !== null;
    }).slice(0, 15).map(e => ({
      tag: e.tagName, id: e.id,
      cls: (typeof e.className === 'string' ? e.className : '').slice(0, 160),
      text: (e.innerText || '').replace(/\\s+/g, ' ').slice(0, 80),
    }));
    return { url: location.href, title: document.title, fixedLayers: all, iframes: [...document.querySelectorAll('iframe')].map(f => f.src).slice(0, 10) };
  })()`);

  fs.writeFileSync(path.join(OUT, 'login-probe.json'),
    JSON.stringify({ url: URL_, seconds: SECONDS, hits, samples, final }, null, 2), 'utf8');

  // screenshot for eyeballing
  const shot = await send('Page.captureScreenshot', { format: 'png' }).catch(() => null);
  if (shot?.data) fs.writeFileSync(path.join(OUT, 'login-probe.png'), Buffer.from(shot.data, 'base64'));

  console.log('probe done: hits=' + (hits ? hits.length : 0));
  console.log(JSON.stringify(final, null, 2).slice(0, 2000));
  process.exit(0);
})().catch((e) => { console.error('PROBE FAILED:', e.message); process.exit(1); });
