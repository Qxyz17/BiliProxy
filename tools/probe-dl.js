'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9362;
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
  const profile = path.join(process.env.TEMP, 'edge-dlchk');
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
  await sleep(12000); // 等播放器跑起来,playurl 经过代理缓存

  // 1) 按钮挂载检查
  const btn = await send('Runtime.evaluate', {
    expression: `(function(){
      var h = document.querySelector('.video-toolbar-left');
      var b = h ? h.lastElementChild : null;
      return JSON.stringify({toolbar: !!h, lastText: b ? b.textContent : null,
        flag: window.__biliProxyDl === 1});
    })()`,
    returnByValue: true,
  });
  console.log('btn:', btn.result.value);

  // 2) 页面内直接发起下载请求(模拟点击,带 20s 超时)
  const dl = await send('Runtime.evaluate', {
    expression: `(function(){
      return new Promise(function(resolve){
        var t = setTimeout(function(){ resolve('HANG:fetch 未在20秒内返回'); }, 20000);
        fetch('/__biliproxy/dl?wm=0&name=' + encodeURIComponent('探针测试'), {cache:'no-store'})
          .then(function(r){ return r.text(); })
          .then(function(tx){ clearTimeout(t); resolve('RESP:' + tx.slice(0,200)); })
          .catch(function(e){ clearTimeout(t); resolve('ERR:' + e.message); });
      });
    })()`,
    returnByValue: true,
    awaitPromise: true,
  });
  console.log('dl:', dl.result.value);
  fs.writeFileSync(path.join(OUT, 'dl-check.txt'),
    String(btn.result.value) + '\n' + String(dl.result.value));
  process.exit(0);
})().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
