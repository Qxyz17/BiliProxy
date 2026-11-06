'use strict';
// 校验 /__biliproxy/ai 转发是否带上了 Content-Length
const http = require('http');
const tls = require('tls');

function call(path, method, body, extraHeaders) {
  return new Promise((resolve, reject) => {
    const req = http.request({ host: '127.0.0.1', port: 18080, method: 'CONNECT', path: 'www.bilibili.com:443' });
    req.on('connect', (res, socket) => {
      const s = tls.connect({ socket, servername: 'www.bilibili.com', rejectUnauthorized: false }, () => {
        let h = method + ' ' + path + ' HTTP/1.1\r\nHost: www.bilibili.com\r\nConnection: close\r\n';
        if (body) h += 'Content-Type: application/json\r\nContent-Length: ' + Buffer.byteLength(body) + '\r\n';
        for (const k in extraHeaders || {}) h += k + ': ' + extraHeaders[k] + '\r\n';
        s.write(h + '\r\n' + (body || ''));
      });
      const chunks = [];
      s.on('data', (d) => chunks.push(d));
      s.on('error', reject);
      setTimeout(() => { s.destroy(); resolve(Buffer.concat(chunks).toString('utf8')); }, 30000);
    });
    req.on('error', reject);
    req.end();
  });
}

(async () => {
  // httpbin 会回显收到的请求，用来确认 Content-Length 真的发出去了
  const probe = JSON.stringify({
    url: 'https://httpbin.org/post',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ hello: 'world', n: 42 }),
  });
  let raw = await call('/__biliproxy/ai', 'POST', probe);
  let p = raw.indexOf('\r\n\r\n');
  console.log('=== 转发到 httpbin 验证 body 完整性 ===');
  const j = JSON.parse(raw.slice(p + 4));
  console.log('ok:', j.ok, 'status:', j.status);
  if (j.ok) {
    const inner = JSON.parse(j.text);
    console.log('回显 data 长度:', (inner.data || '').length);
    console.log('回显 data:', inner.data);
    console.log('回显 headers Content-Length:', inner.headers && inner.headers['Content-Length']);
  } else {
    console.log('text:', String(j.text || '').slice(0, 200));
  }
})();
