'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9363;
const OUT = path.join(__dirname, '..', '_probe');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ws, seq = 0;
const pending = new Map();

function send(m, p = {}) {
  const id = ++seq;
  ws.send(JSON.stringify({ id, method: m, params: p }));
  return new Promise((res, rej) => {
    pending.set(id, { res, rej });
    setTimeout(() => { if (pending.has(id)) { pending.delete(id); rej(new Error('timeout ' + m)); } }, 60000);
  });
}

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const profile = path.join(process.env.TEMP, 'edge-taster');
  fs.rmSync(profile, { recursive: true, force: true });
  spawn(EDGE, ['--headless=new', '--disable-gpu', '--no-first-run', '--mute-audio',
    `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
    '--proxy-server=http://127.0.0.1:18080', '--ignore-certificate-errors',
    '--window-size=1280,900',
    'https://www.bilibili.com/video/BV1nFzdYfEgs/'], { stdio: 'ignore' });

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
  await sleep(10000);

  const st = await send('Runtime.evaluate', {
    expression: `(function(){
      var root = document.querySelector('#bilitaster-root');
      var tog = document.querySelector('.bt-toggle');
      var r = root ? root.getBoundingClientRect() : null;
      return JSON.stringify({
        loaded: window.__BILITASTER_LOADED__ === true,
        shim: window.__btShim === 1,
        hasChrome: typeof window.chrome === 'object' && !!window.chrome.storage,
        root: !!root,
        toggle: !!tog,
        pos: r ? {right: Math.round(window.innerWidth - r.right), bottom: Math.round(window.innerHeight - r.bottom), w: Math.round(r.width), h: Math.round(r.height)} : null,
        cssOk: tog ? getComputedStyle(tog).borderRadius : null,
        dlBtn: !!document.querySelector('#bpDl'),
      });
    })()`,
    returnByValue: true,
  });
  console.log('state:', st.result.value);

  const click = await send('Runtime.evaluate', {
    expression: `(function(){
      var t = document.querySelector('.bt-toggle');
      if(!t) return 'no toggle';
      t.click();
      var p = document.querySelector('.bt-panel');
      return JSON.stringify({panel: !!p, hidden: p ? p.classList.contains('hidden') : null});
    })()`,
    returnByValue: true,
  });
  console.log('open:', click.result.value);

  const save = await send('Runtime.evaluate', {
    expression: `(function(){
      return new Promise(function(resolve){
        var root = document.querySelector('#bilitaster-root');
        if(!root) return resolve('no root');
        var b = root.querySelector('.bt-base'), k = root.querySelector('.bt-key'), m = root.querySelector('.bt-model');
        if(!b) return resolve('no settings inputs');
        b.value = 'https://api.openai.com/v1';
        k.value = 'sk-test';
        m.value = 'gpt-4o-mini';
        root.querySelector('[data-act="save"]') ? root.querySelector('[data-act="save"]').click() : null;
        setTimeout(function(){
          var raw = {};
          for(var i=0;i<localStorage.length;i++){var key=localStorage.key(i); if(key.indexOf('bt:')===0) raw[key]=localStorage.getItem(key);}
          resolve(JSON.stringify(raw));
        }, 500);
      });
    })()`,
    returnByValue: true,
    awaitPromise: true,
  });
  console.log('save:', save.result.value);

  const shots = await send('Page.captureScreenshot', { format: 'png' });
  fs.writeFileSync(path.join(OUT, 'taster.png'), Buffer.from(shots.data, 'base64'));
  console.log('shot: _probe/taster.png');
  process.exit(0);
})().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
