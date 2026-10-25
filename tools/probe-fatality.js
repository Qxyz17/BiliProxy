'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9341;
const OUT = path.join(__dirname, '..', '_probe');
const URL_ = process.argv[2] || 'https://www.bilibili.com/video/BV1nFzdYfEgs/';
const TIMES = (process.argv[3] || '400,1200,2000,3000').split(',').map(Number);

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ws, seq = 0;
const pending = new Map();

function send(method, params = {}) {
  const id = ++seq;
  ws.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    setTimeout(() => { if (pending.has(id)) { pending.delete(id); reject(new Error('timeout ' + method)); } }, 25000);
  });
}

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const profile = path.join(process.env.TEMP, 'edge-fatality');
  fs.rmSync(profile, { recursive: true, force: true });
  spawn(EDGE, ['--headless=new', '--disable-gpu', '--no-first-run', '--mute-audio',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
    '--proxy-server=http://127.0.0.1:18080', '--ignore-certificate-errors',
    '--window-size=1280,800', URL_], { stdio: 'ignore' });

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

  for (const t of TIMES) {
    await sleep(t === TIMES[0] ? t : t - TIMES[TIMES.indexOf(t) - 1]);
    const shot = await send('Page.captureScreenshot', { format: 'png' }).catch(() => null);
    if (shot?.data) {
      fs.writeFileSync(path.join(OUT, `fat_${t}.png`), Buffer.from(shot.data, 'base64'));
    }
    const info = await send('Runtime.evaluate', {
      expression: `({f: !!document.querySelector('iframe[src*="fatality"]')})`, returnByValue: true,
    }).catch(() => null);
    console.log(t + 'ms', info?.result?.value);
  }
  process.exit(0);
})().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
