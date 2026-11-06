'use strict';
// 验证：用户手动暂停不会被自动续播顶掉
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9369;
const BV = process.argv[2] || 'BV1Fz2eBmE9r';

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ws, seq = 0;
const pending = new Map();

function send(m, p = {}) {
  const id = ++seq;
  ws.send(JSON.stringify({ id, method: m, params: p }));
  return new Promise((res, rej) => {
    pending.set(id, { res, rej });
    setTimeout(() => { if (pending.has(id)) { pending.delete(id); rej(new Error('timeout ' + m)); } }, 120000);
  });
}

async function evalJs(expression, awaitPromise) {
  const r = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: !!awaitPromise });
  if (r.exceptionDetails) return 'EXC: ' + (r.exceptionDetails.exception && r.exceptionDetails.exception.description);
  return r.result && r.result.value;
}

(async () => {
  const profile = path.join(process.env.TEMP, 'edge-pause');
  fs.rmSync(profile, { recursive: true, force: true });
  spawn(EDGE, ['--headless=new', '--disable-gpu', '--no-first-run', '--mute-audio',
    `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
    '--proxy-server=http://127.0.0.1:18080', '--ignore-certificate-errors',
    '--window-size=1280,900', '--autoplay-policy=no-user-gesture-required',
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

  // 开始播放
  await evalJs(`(function(){var v=document.querySelector('video'); if(v){v.play().catch(function(){});} })()`);
  await sleep(3000);
  console.log('playing:', await evalJs(`(function(){var v=document.querySelector('video');
    return JSON.stringify({paused:v.paused, t:Math.round(v.currentTime)});})()`));

  // 模拟用户按暂停：先派发 pointerdown（脚本据此认定是用户操作），再 pause
  await evalJs(`(function(){
    var v=document.querySelector('video');
    var b=document.querySelector('.bpx-player-ctrl-play')||document.body;
    ['pointerdown','mousedown'].forEach(function(t){
      b.dispatchEvent(new MouseEvent(t,{bubbles:true,cancelable:true}));
    });
    v.pause();
  })()`);
  console.log('after user pause:', await evalJs(`(function(){var v=document.querySelector('video');
    return JSON.stringify({paused:v.paused, t:Math.round(v.currentTime)});})()`));

  // 等 5 秒，看会不会被自动续播
  await sleep(5000);
  const after = await evalJs(`(function(){var v=document.querySelector('video');
    return JSON.stringify({paused:v.paused, t:Math.round(v.currentTime),
      userPaused: v.__bpHook ? 'hooked' : 'nohook'});})()`);
  console.log('after 5s:', after);

  // 再验证：用户点播放后仍然正常
  await evalJs(`(function(){
    var v=document.querySelector('video');
    document.body.dispatchEvent(new MouseEvent('pointerdown',{bubbles:true}));
    v.play().catch(function(){});
  })()`);
  await sleep(2500);
  console.log('resume ok:', await evalJs(`(function(){var v=document.querySelector('video');
    return JSON.stringify({paused:v.paused, t:Math.round(v.currentTime)});})()`));

  const s = await send('Page.captureScreenshot', { format: 'png' });
  fs.mkdirSync(path.join(__dirname, '..', '_probe'), { recursive: true });
  fs.writeFileSync(path.join(__dirname, '..', '_probe', 'pause.png'), Buffer.from(s.data, 'base64'));
  process.exit(0);
})().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
