const http = require('http');
const tls = require('tls');

function fetchVia(host, path, hostHdr) {
  return new Promise((resolve, reject) => {
    const req = http.request({ host: '127.0.0.1', port: 18080, method: 'CONNECT', path: host + ':443' });
    req.on('connect', (res, socket) => {
      const s = tls.connect({ socket, servername: host, rejectUnauthorized: false }, () => {
        s.write('GET ' + path + ' HTTP/1.1\r\nHost: ' + (hostHdr || host) + '\r\n' +
                'User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/109.0.0.0 Safari/537.36 Edg/109.0.1518.78\r\n' +
                'Accept: text/html,application/xhtml+xml\r\nAccept-Language: zh-CN,zh;q=0.9\r\n' +
                'Connection: close\r\n\r\n');
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
  let raw = await fetchVia('www.bilibili.com', '/video/BV1nFzdYfEgs');
  let p = raw.indexOf('\r\n\r\n');
  let head = raw.slice(0, p), body = raw.slice(p + 4);
  console.log('1st:', head.split('\r\n')[0]);
  const loc = (head.match(/^location:\s*(.+)$/im) || [])[1];
  if (loc) {
    console.log('location:', loc.trim());
    const u = new URL(loc.trim(), 'https://www.bilibili.com/video/BV1nFzdYfEgs');
    raw = await fetchVia(u.host, u.pathname + u.search, u.host);
    p = raw.indexOf('\r\n\r\n');
    head = raw.slice(0, p);
    body = raw.slice(p + 4);
    console.log('2nd:', head.split('\r\n')[0]);
  }
  console.log('body bytes:', body.length);
  const keys = ['__BILITASTER_LOADED__', 'bt-toggle', 'bilitaster-root', '__btShim',
                '--bt-accent', '__biliProxyDl', '__biliProxyFatality',
                '/__biliproxy/fetch', '/__biliproxy/ai', 'bili-mini', 'login-pwd-wp'];
  for (const k of keys) console.log('  ' + k + ' : ' + body.includes(k));
  require('fs').writeFileSync(process.env.TEMP + '\\bp_verify.html', body);
})();
