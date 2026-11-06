'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9366;
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
    setTimeout(() => { if (pending.has(id)) { pending.delete(id); rej(new Error('timeout ' + m)); } }, 120000);
  });
}

async function evalJs(expression, awaitPromise) {
  const r = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: !!awaitPromise });
  if (r.exceptionDetails) return 'EXC: ' + (r.exceptionDetails.exception && r.exceptionDetails.exception.description);
  return r.result && r.result.value;
}

async function shot(name) {
  const s = await send('Page.captureScreenshot', { format: 'png' });
  fs.writeFileSync(path.join(OUT, name), Buffer.from(s.data, 'base64'));
}

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const profile = path.join(process.env.TEMP, 'edge-rail');
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
  await sleep(11000);

  // 对话面板
  await evalJs(`document.querySelector('#bpRail .bpBtn[data-k="chat"]').click()`);
  await sleep(800);
  console.log('chat:', await evalJs(`(function(){
    var p=document.querySelector('#bpPanel');
    return JSON.stringify({open:p.classList.contains('open'), op:getComputedStyle(p).opacity,
      title:(p.querySelector('.bpVtitle')||{}).textContent,
      meta:(p.querySelector('.bpVmeta')||{}).textContent});
  })()`));

  await evalJs(`(function(){
    var c=document.querySelector('#bpPanel .bpChat');
    c.innerHTML='<div class="bpMsg user">总结这个视频</div>'+
      '<div class="bpMsg sys">已获取：字幕 1842 字，弹幕 200 条，评论 3 条，24 帧画面</div>'+
      '<div class="bpMsg ai"><p>这是一段<strong>录音棚试听</strong>视频，全程是《出山》的完整播放。</p>'+
      '<ul><li>0:00 起前奏，画面为录音棚全景</li><li>1:20 切入主歌，字幕同步滚动</li>'+
      '<li>2:40 副歌段落，机位拉近音箱</li></ul><p>整体没有解说，属于纯听感向内容。</p></div>';
  })()`);
  await sleep(400);
  await shot('rail-chat.png');

  // 设置页
  await evalJs(`document.querySelector('#bpPanel [data-act="settings"]').click()`);
  await sleep(600);
  console.log('settings:', await evalJs(`(function(){
    var s=document.querySelector('#bpPanel .bpSet');
    return JSON.stringify({open:s.classList.contains('open'),
      chk:s.querySelectorAll('.bpChk').length, fld:s.querySelectorAll('.bpField').length,
      fpm:(s.querySelector('.s-fpm')||{}).value, mode:(s.querySelector('.s-fmode')||{}).value});
  })()`));
  await shot('rail-settings.png');

  await evalJs(`document.querySelector('#bpPanel .bpSet [data-act="back"]').click()`);
  await sleep(300);

  // 下载浮层 + 真下载
  await evalJs(`document.querySelector('#bpRail .bpBtn[data-k="down"]').click()`);
  await sleep(600);
  console.log('dlpop:', await evalJs(`(function(){
    var d=document.querySelector('#bpDlPop');
    return JSON.stringify({open:d.classList.contains('open'),
      opts:d.querySelectorAll('.bpDlOpt').length,
      panelClosed:!document.querySelector('#bpPanel').classList.contains('open')});
  })()`));
  await shot('rail-dl.png');

  console.log('dljob:', await evalJs(`(async function(){
    var vd=window.__INITIAL_STATE__&&window.__INITIAL_STATE__.videoData;
    var aid=vd&&vd.aid||'', cid=vd&&vd.cid||'';
    var nm=encodeURIComponent((document.title||'').replace(/^【.*?】/,'').replace(/_哔哩哔哩_bilibili$/,'').slice(0,60));
    var r=await fetch('/__biliproxy/dl?wm=0&name='+nm+'&aid='+aid+'&cid='+cid,{cache:'no-store'});
    var j=await r.json();
    if(!j.ok) return 'FAIL '+j.msg;
    for(var i=0;i<40;i++){
      await new Promise(function(ok){setTimeout(ok,700);});
      var s=await (await fetch('/__biliproxy/dlstatus?id='+j.id,{cache:'no-store'})).json();
      if(s.state===2) return 'OK '+s.out;
      if(s.state===3) return 'FAIL '+s.note;
    }
    return 'TIMEOUT';
  })()`, true));

  process.exit(0);
})().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
