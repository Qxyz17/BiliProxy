'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9339;
const URL_ = process.argv[2] || 'https://www.bilibili.com/video/BV1nFzdYfEgs/';

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ws;
let seq = 0;
const pending = new Map();

function send(method, params = {}) {
  const id = ++seq;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    setTimeout(() => { if (pending.has(id)) { pending.delete(id); reject(new Error('timeout ' + method)); } }, 20000);
  });
}

async function ev(expr) {
  const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
  return r.result.value;
}

(async () => {
  const profile = path.join(process.env.TEMP, 'edge-bili-login');
  fs.rmSync(profile, { recursive: true, force: true });
  spawn(EDGE, ['--headless=new', '--disable-gpu', '--no-first-run', '--mute-audio',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
    '--proxy-server=http://127.0.0.1:18080', '--window-size=1280,900', URL_], { stdio: 'ignore' });

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
  ws.addEventListener('message', async (e) => {
    let raw = e.data;
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
  await sleep(8000);

  const before = await ev(`({injected:!!window.__biliproxyNoLogin, mini:document.querySelectorAll('[class*="bili-mini"]').length, loginEntry:!!document.querySelector('[class*="unlogin"]')})`);

  const clicked = await ev(`(() => {
    const el = document.querySelector('[class*="unlogin-entry"]') || document.querySelector('[class*="unlogin"]');
    if (!el) return 'no-entry';
    el.click();
    return 'clicked';
  })()`);
  await sleep(4000);

  const after = await ev(`(() => {
    const n = document.querySelectorAll('[class*="bili-mini"]');
    const vis = [...n].filter(e => e.offsetParent !== null).length;
    const dialogs = [...document.querySelectorAll('[class*="dialog"],[class*="modal"]')]
      .filter(e => e.offsetParent !== null && /登录|扫码/.test(e.innerText || '')).length;
    return { mini: n.length, visible: vis, loginDialogs: dialogs };
  })()`);

  console.log(JSON.stringify({ before, clicked, after }, null, 2));
  process.exit(0);
})().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
