'use strict';

/**
 * Headless Edge + CDP: watch a Bilibili video page as guest, record which
 * network JSON names appear around each auto login popup, and when.
 * Also snapshots DOM when a bili-mini overlay is injected.
 */

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9345;
const URL_ = 'https://www.bilibili.com/video/BV1nFzdYfEgs/';
const OUT = path.join(__dirname, '..', '_probe');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ws, seq = 0;
const pending = new Map();
const events = [];

function send(method, params = {}) {
  const id = ++seq;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    setTimeout(() => { if (pending.has(id)) { pending.delete(id); reject(new Error('timeout ' + method)); } }, 25000);
  });
}

async function ev(expr) {
  const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
  return r.result.value;
}

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const profile = path.join(process.env.TEMP, 'edge-bili-guest');
  fs.rmSync(profile, { recursive: true, force: true });
  spawn(EDGE, ['--headless=new', '--disable-gpu', '--no-first-run', '--mute-audio',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
    '--window-size=1280,900', URL_], { stdio: 'ignore' });

  let target;
  for (let i = 0; i < 80; i++) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
      target = list.find((t) => t.type === 'page' && t.url.includes('bilibili.com'));
      if (target) break;
    } catch {}
    await sleep(250);
  }
  if (!target) throw new Error('no target');

  ws = new WebSocket(target.webSocketDebuggerUrl);
  ws.addEventListener('message', (e) => {
    let raw = e.data;
    if (typeof raw !== 'string') { /* events are small strings here */ }
    const m = JSON.parse(raw);
    if (m.id && pending.has(m.id)) {
      const { resolve, reject } = pending.get(m.id);
      pending.delete(m.id);
      if (m.error) reject(new Error(m.error.message)); else resolve(m.result);
      return;
    }
    if (m.method === 'Network.responseReceived') {
      const r = m.params.response;
      if (r.mimeType.includes('json') && r.status === 200) {
        events.push({ t: performance.now() / 1000, url: r.url.slice(0, 160) });
      }
    }
  });
  await new Promise((res, rej) => { ws.addEventListener('open', res); ws.addEventListener('error', rej); });
  await send('Page.enable');
  await send('Network.enable');

  const marks = [];
  const timer = setInterval(async () => {
    try {
      const r = await ev(`(() => {
        const n = document.querySelectorAll('[class*="bili-mini"]');
        const vis = [...n].some(e => e.offsetParent !== null || getComputedStyle(e).display !== 'none');
        return { mini: n.length, vis, hasBanner: !!document.querySelector('.login-banner,[class*="login-pwd-wp"]') };
      })()`);
      if (r.vis || r.hasBanner) marks.push({ t: performance.now() / 1000, ...r });
    } catch {}
  }, 500);

  await sleep(240000);
  clearInterval(timer);

  fs.writeFileSync(path.join(OUT, 'guest-login-trace.json'),
    JSON.stringify({ url: URL_, popupMarks: marks, jsonRequests: events }, null, 2), 'utf8');
  console.log('popups at:', marks.map(m => m.t.toFixed(1) + 's').join(', ') || 'none');
  console.log('json responses captured:', events.length);
  process.exit(0);
})().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
