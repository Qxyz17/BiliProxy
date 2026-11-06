'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const EDGE = process.env.EDGE_PATH || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const PORT = 9368;
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
  if (r.exceptionDetails) return 'EXC: ' + (r.exceptionDetails.exception && r.exceptionDetails.exception.description);
  return r.result && r.result.value;
}

const ANSWER = [
  '## 视频内容',
  '这是一段**录音棚试听**，全程只有《出山》的完整播放，没有解说。',
  '',
  '### 段落结构',
  '1. `0:00` 起前奏，机位固定在全景',
  '2. `1:20` 进主歌，字幕开始滚动',
  '3. `2:40` 副歌段落，镜头拉近音箱',
  '',
  '### 要点',
  '- 音频是 Hi-Res 规格',
  '- 画面切换很少，共 4 个机位',
  '- 弹幕集中在副歌（约 `2:40`）',
  '',
  '> 适合戴着耳机整首听完，不适合当作讲解类内容。',
  '',
  '| 项目 | 数值 |',
  '|---|---|',
  '| 时长 | 3:20 |',
  '| 机位 | 4 |',
  '',
  '```text',
  '0:00 ── 前奏',
  '1:20 ── 主歌',
  '2:40 ── 副歌',
  '```',
].join('\n');

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const profile = path.join(process.env.TEMP, 'edge-md2');
  fs.rmSync(profile, { recursive: true, force: true });
  spawn(EDGE, ['--headless=new', '--disable-gpu', '--no-first-run', '--mute-audio',
    `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
    '--proxy-server=http://127.0.0.1:18080', '--ignore-certificate-errors',
    '--window-size=1400,1100', '--autoplay-policy=no-user-gesture-required',
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

  // 先把接口配置写进 localStorage，再拦 AI 转发返回固定内容，走真实的 addMsg 渲染路径
  await evalJs(`(function(){
    localStorage.setItem('bp:baseUrl', JSON.stringify('https://api.test/v1'));
    localStorage.setItem('bp:apiKey', JSON.stringify('k'));
    localStorage.setItem('bp:model', JSON.stringify('m'));
    localStorage.setItem('bp:opts', JSON.stringify({framePerMin:0, frameMode:'fixed', frameFixed:1,
      subtitle:false, danmaku:false, comments:false, title:true, desc:false, owner:false}));
    location.reload();
  })()`);
  await sleep(12000);

  await evalJs(`document.querySelector('#bpRail .bpBtn[data-k="chat"]').click()`);
  await sleep(600);

  await evalJs(`window.__origFetch = window.fetch;
    window.fetch = function(u, o){
      var url = typeof u === 'string' ? u : (u && u.url) || '';
      if (url.indexOf('/__biliproxy/ai') === 0) {
        return Promise.resolve(new Response(JSON.stringify({ok:true, status:200,
          text: JSON.stringify({choices:[{message:{content: ${JSON.stringify(ANSWER)}}}]})}),
          {status:200, headers:{'Content-Type':'application/json'}}));
      }
      if (url.indexOf('/__biliproxy/frames') === 0) {
        return Promise.resolve(new Response(JSON.stringify({ok:false,msg:'skip'}), {status:200}));
      }
      return window.__origFetch.apply(this, arguments);
    };`);

  await evalJs(`(function(){
    var i=document.querySelector('#bpPanel .bpInput');
    i.value='反点反点任意内容\\ntfuudduufxfdcjcnckcj\\n反反反';
    document.querySelector('#bpPanel [data-act="send"]').click();
  })()`);
  await sleep(3000);

  console.log('chat:', await evalJs(`(function(){
    var msgs=document.querySelectorAll('#bpPanel .bpChat .bpMsg');
    var ai=document.querySelector('#bpPanel .bpChat .bpMsg.ai');
    if(!ai) return 'no ai msg';
    var tags={};
    [].forEach.call(ai.querySelectorAll('*'),function(n){tags[n.tagName]=(tags[n.tagName]||0)+1;});
    var user=document.querySelector('#bpPanel .bpChat .bpMsg.user');
    return JSON.stringify({
      msgCount: msgs.length,
      tags: tags,
      rawLeft: /\\*\\*|^##|\\| ---/.test(ai.textContent),
      userText: user ? user.textContent.slice(0,40) : null,
      userHtmlBr: user ? user.querySelectorAll('br').length : 0,
      aiTextHead: ai.textContent.slice(0,50)
    });
  })()`));

  const shot = await send('Page.captureScreenshot', { format: 'png' });
  fs.writeFileSync(path.join(OUT, 'md.png'), Buffer.from(shot.data, 'base64'));
  console.log('shot: _probe/md.png');
  process.exit(0);
})().catch((e) => { console.error('FAIL', e.message); process.exit(1); });
