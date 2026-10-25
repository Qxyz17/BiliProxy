'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9347;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ws, seq = 0;
const pending = new Map();

function send(method, params = {}) {
  const id = ++seq;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    setTimeout(() => { if (pending.has(id)) { pending.delete(id); reject(new Error('timeout')); } }, 20000);
  });
}

async function ev(expr) {
  const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || 'eval error');
  return r.result.value;
}

(async () => {
  const profile = path.join(process.env.TEMP, 'edge-nologin-test');
  fs.rmSync(profile, { recursive: true, force: true });
  spawn(EDGE, ['--headless=new', '--disable-gpu', '--no-first-run', '--mute-audio',
    `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
    '--proxy-server=http://127.0.0.1:18080', '--ignore-certificate-errors',
    '--window-size=1280,900', 'https://www.bilibili.com/video/BV1nFzdYfEgs/'], { stdio: 'ignore' });

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
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) {
      const { resolve, reject } = pending.get(m.id);
      pending.delete(m.id);
      if (m.error) reject(new Error(m.error.message)); else resolve(m.result);
    }
  });
  await new Promise((res, rej) => { ws.addEventListener('open', res); ws.addEventListener('error', rej); });
  await send('Page.enable');
  await sleep(8000);

  const injected = await ev('!!window.__biliproxyNoLogin');
  await ev(`(() => {
    const d = document.createElement('div');
    d.className = 'bili-mini-mask';
    d.id = 'probe-auto';
    d.style.cssText = 'position:fixed;inset:0;z-index:99999;background:#000';
    document.body.appendChild(d);
    const v = document.createElement('video');
    return true;
  })()`);
  await sleep(2600);
  const removed = await ev(`!document.getElementById('probe-auto')`);

  const resume = await ev(`(() => {
    const v = document.createElement('video');
    if (!v) return 'no-video';
    Object.defineProperty(v, 'paused', { get: () => true, configurable: true });
    Object.defineProperty(v, 'ended', { get: () => false, configurable: true });
    Object.defineProperty(v, 'currentTime', { get: () => 10, configurable: true });
    let played = false;
    v.play = () => { played = true; return Promise.resolve(); };
    const d = document.createElement('div');
    d.className = 'bili-mini-mask';
    document.body.appendChild(d);
    return new Promise((r) => setTimeout(() => r({ played, gone: !document.contains(d) }), 2600));
  })()`);

  console.log(JSON.stringify({ injected, removed, resume }, null, 2));
  process.exit(0);
})().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
