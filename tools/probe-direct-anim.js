'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9349;
const OUT = path.join(__dirname, '..', '_probe');
const URL_ = 'https://www.bilibili.com/__biliproxy/fatality.html?clear=1';

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ws, seq = 0;
const pending = new Map();

function send(m, p = {}) {
  const id = ++seq;
  ws.send(JSON.stringify({ id, method: m, params: p }));
  return new Promise((res, rej) => {
    pending.set(id, { res, rej });
    setTimeout(() => { if (pending.has(id)) { pending.delete(id); rej(new Error('timeout')); } }, 20000);
  });
}

(async () => {
  const profile = path.join(process.env.TEMP, 'edge-fat-direct');
  fs.rmSync(profile, { recursive: true, force: true });
  spawn(EDGE, ['--headless=new', '--disable-gpu', '--no-first-run', '--mute-audio',
    `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
    '--proxy-server=http://127.0.0.1:18080', '--ignore-certificate-errors',
    '--window-size=1280,900', URL_], { stdio: 'ignore' });

  let target;
  for (let i = 0; i < 80; i++) {
    try {
      const l = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
      target = l.find((t) => t.type === 'page' && t.url.includes('fatality'));
      if (target) break;
    } catch {}
    await sleep(250);
  }
  if (!target) throw new Error('no target');
  ws = new WebSocket(target.webSocketDebuggerUrl);
  ws.addEventListener('message', (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) {
      const { res, rej } = pending.get(m.id);
      pending.delete(m.id);
      m.error ? rej(new Error(m.error.message)) : res(m.result);
    }
  });
  await new Promise((r, j) => { ws.addEventListener('open', r); ws.addEventListener('error', j); });
  await send('Page.enable');
  await sleep(1100);
  const st = await send('Runtime.evaluate', {
    expression: `({w: innerWidth, h: innerHeight, cw: document.getElementById('cv').width, ch: document.getElementById('cv').height})`,
    returnByValue: true,
  });
  const shot = await send('Page.captureScreenshot', { format: 'png' });
  fs.writeFileSync(path.join(OUT, 'direct_anim.png'), Buffer.from(shot.data, 'base64'));
  console.log(JSON.stringify(st.result.value));
  process.exit(0);
})().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
