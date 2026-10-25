'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9361;
const OUT = path.join(__dirname, '..', '_probe');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let ws, seq = 0;
const pending = new Map();

function send(m, p = {}) {
  const id = ++seq;
  ws.send(JSON.stringify({ id, method: m, params: p }));
  return new Promise((res, rej) => {
    pending.set(id, { res, rej });
    setTimeout(() => { if (pending.has(id)) { pending.delete(id); rej(new Error('timeout')); } }, 30000);
  });
}

const HOOK = `
(function(){
  window.__pl = [];
  function body(txt){
    try {
      var j = JSON.parse(txt);
      var v = j && j.data && j.data.dash && j.data.dash.video;
      if (v) window.__pl.push(v.map(function(x){return x.codecid + ':' + (x.id||'?');}).join(','));
    } catch(e){}
  }
  var of = window.fetch;
  window.fetch = function(u, o){
    var p = of.apply(this, arguments);
    var s = String(u && u.url || u);
    if (s.indexOf('/playurl') >= 0) {
      p.then(function(r){ return r.clone().text(); }).then(body).catch(function(){});
    }
    return p;
  };
  var oo = XMLHttpRequest.prototype.open;
  var os = XMLHttpRequest.prototype.send;
  XMLHttpRequest.prototype.open = function(m, u){ this.__u = u; return oo.apply(this, arguments); };
  XMLHttpRequest.prototype.send = function(){
    if (String(this.__u||'').indexOf('/playurl') >= 0) {
      this.addEventListener('load', function(){ body(this.responseText); });
    }
    return os.apply(this, arguments);
  };
})();`;

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const profile = path.join(process.env.TEMP, 'edge-codecchk');
  fs.rmSync(profile, { recursive: true, force: true });
  spawn(EDGE, ['--headless=new', '--disable-gpu', '--no-first-run', '--mute-audio',
    `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
    '--proxy-server=http://127.0.0.1:18080', '--ignore-certificate-errors',
    '--window-size=1280,900', '--autoplay-policy=no-user-gesture-required',
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
  await send('Page.addScriptToEvaluateOnNewDocument', { source: HOOK });
  await send('Page.reload');
  await sleep(9000);

  const r = await send('Runtime.evaluate', {
    expression: `JSON.stringify((function(){
      var v = document.querySelector('video');
      var st = v ? {ready: v.readyState, ct: v.currentTime, paused: v.paused, err: (v.error && v.error.code) || 0, sw: v.videoWidth, sh: v.videoHeight} : null;
      return {playUrls: window.__pl, video: st};
    })())`,
    returnByValue: true,
  });
  console.log('probe:', r.result.value);
  fs.writeFileSync(path.join(OUT, 'codec-check.json'), r.result.value || '');
  process.exit(0);
})().catch((e) => { console.error('FAIL', e.message); process.exit(1); });

