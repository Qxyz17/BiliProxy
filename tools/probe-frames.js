'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9365;
const OUT = path.join(__dirname, '..', '_probe');
const BV = process.argv[2] || 'BV1Fz2eBmE9r';

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ws, seq = 0;
const pending = new Map();

function send(m, p = {}) {
  const id = ++seq;
  ws.send(JSON.stringify({ id, method: m, params: p }));
  return new Promise((res, rej) => {
    pending.set(id, { res, rej });
    setTimeout(() => { if (pending.has(id)) { pending.delete(id); rej(new Error('timeout ' + m)); } }, 180000);
  });
}

async function evalJs(expression, awaitPromise) {
  const r = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: !!awaitPromise });
  if (r.exceptionDetails) return 'EXC: ' + JSON.stringify(r.exceptionDetails.exception && r.exceptionDetails.exception.description);
  return r.result && r.result.value;
}

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const profile = path.join(process.env.TEMP, 'edge-frames');
  fs.rmSync(profile, { recursive: true, force: true });
  spawn(EDGE, ['--headless=new', '--disable-gpu', '--no-first-run', '--mute-audio',
    `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
    '--proxy-server=http://127.0.0.1:18080', '--ignore-certificate-errors',
    '--window-size=1400,900', '--autoplay-policy=no-user-gesture-required',
    `https://www.bilibili.com/video/${BV}/`], { stdio: 'ignore' });

  let target;
  for (let i = 0; i < 80; i++) {
    try {
      const l = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
      target = l.find((t) => t.type === 'page' && t.url.includes('bilibili'));
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
  await send('Runtime.enable');
  await sleep(12000);

  console.log('panel:', await evalJs(`(function(){
    var p = document.querySelector('#bpPanel');
    var chat = p && p.querySelector('.bpVtitle');
    var info = window.__INITIAL_STATE__ && window.__INITIAL_STATE__.videoData;
    return JSON.stringify({
      rail: !!document.querySelector('#bpRail'),
      panel: !!p,
      oldDl: !!document.querySelector('#bpDl'),
      oldTaster: !!document.querySelector('#bilitaster-root'),
      title: chat ? chat.textContent : null,
      cid: info ? info.cid : null,
      dur: info ? info.duration : null,
      vDur: (function(){var v=document.querySelector('video');return v?v.duration:null;})()
    });
  })()`));

  // 直接打 frames 端点，看抽帧数量
  console.log('frames:', await evalJs(`(async function(){
    var info = window.__INITIAL_STATE__ && window.__INITIAL_STATE__.videoData;
    var cid = info ? info.cid : '';
    var t0 = Date.now();
    var r = await fetch('/__biliproxy/frames?cid=' + encodeURIComponent(cid) + '&n=12&w=384', {cache:'no-store'});
    var j = await r.json();
    if(!j.ok) return 'FAIL ' + j.msg;
    return JSON.stringify({n: j.frames.length, ms: Date.now()-t0,
      first: j.frames[0] ? j.frames[0].length : 0,
      head: j.frames[0] ? j.frames[0].slice(0,30) : ''});
  })()`, true));

  const shot = await send('Page.captureScreenshot', { format: 'png' });
  fs.writeFileSync(path.join(OUT, 'rail.png'), Buffer.from(shot.data, 'base64'));
  process.exit(0);
})().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
