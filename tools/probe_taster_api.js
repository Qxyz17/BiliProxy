const http = require('http');
const tls = require('tls');

function call(host, path, method, body) {
  return new Promise((resolve, reject) => {
    const req = http.request({ host: '127.0.0.1', port: 18080, method: 'CONNECT', path: host + ':443' });
    req.on('connect', (res, socket) => {
      const s = tls.connect({ socket, servername: host, rejectUnauthorized: false }, () => {
        let head = method + ' ' + path + ' HTTP/1.1\r\nHost: ' + host + '\r\n' +
                   'Connection: close\r\n';
        if (body) head += 'Content-Type: application/json\r\nContent-Length: ' + Buffer.byteLength(body) + '\r\n';
        s.write(head + '\r\n' + (body || ''));
      });
      const chunks = [];
      s.on('data', (d) => chunks.push(d));
      s.on('error', reject);
      setTimeout(() => {
        s.destroy();
        resolve(Buffer.concat(chunks).toString('utf8'));
      }, 25000);
    });
    req.on('error', reject);
    req.end();
  });
}

(async () => {
  const u = 'https://api.bilibili.com/x/web-interface/view?bvid=BV1nFzdYfEgs';
  let raw = await call('www.bilibili.com', '/__biliproxy/fetch?url=' + encodeURIComponent(u), 'GET');
  let p = raw.indexOf('\r\n\r\n');
  console.log('=== /__biliproxy/fetch ===');
  console.log(raw.slice(0, p).split('\r\n')[0]);
  const j = JSON.parse(raw.slice(p + 4));
  console.log('ok:', j.ok, 'status:', j.status, 'text len:', (j.text || '').length);
  console.log('inner code:', (JSON.parse(j.text).code), 'title:', JSON.parse(j.text).data.title);

  const aiBody = JSON.stringify({
    url: 'https://api.bilibili.com/x/web-interface/view?bvid=BV1nFzdYfEgs',
    headers: { 'Content-Type': 'application/json', 'X-Test': 'v' },
    body: '',
  });
  raw = await call('www.bilibili.com', '/__biliproxy/ai', 'POST', aiBody);
  p = raw.indexOf('\r\n\r\n');
  console.log('=== /__biliproxy/ai ===');
  console.log(raw.slice(0, p).split('\r\n')[0]);
  const a = JSON.parse(raw.slice(p + 4));
  console.log('ok:', a.ok, 'status:', a.status, 'text len:', (a.text || '').length);
})();
