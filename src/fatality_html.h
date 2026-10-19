#pragma once

static const char* kFatalityHtml = R"FATALITY(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>FATALITY</title>
<style>
  /* solid black stage by default; ?clear=1 makes it see-through (overlay use) */
  html,body{margin:0;height:100%;overflow:hidden;background:#000}
  body.clear{background:transparent}
  #stage{position:fixed;inset:0;display:grid;place-items:center;background:transparent}
  /* the blur half of layer 63 (init_anim.cpp:110-115): backdrop-filter blurs
     whatever this page actually sits on, ramping with the same `toggle` */
  #blur{position:fixed;inset:0;pointer-events:none;z-index:1;opacity:0;
    -webkit-backdrop-filter:blur(10px);backdrop-filter:blur(10px)}
  canvas{display:block;position:relative;z-index:2}
</style>
</head>
<body>
<div id="stage"><div id="blur"></div><canvas id="cv"></canvas></div>
<script>
/* ------------------------------------------------------------------
   FATALITY injection intro 鈥?port of
     cs2_internal/src/menu/init/init_anim.cpp  (render() + on_adapter_ready())
   Textures = the exact PNGs embedded in init/resources/{f200,f400,atality}.h
   Tint    = gui.h  MAKE_COLOR(accent, 235, 5, 90)
   Background = none: the canvas is transparent, the 75% black veil is part
   of the animation, the blur is a CSS backdrop-filter on top of whatever
   is behind the page.
   ------------------------------------------------------------------ */
(() => {
  const QS     = new URLSearchParams(location.search);
  const EXPORT = QS.has('frame') || QS.has('export');
  const FRAME  = +QS.get('frame') || 0;
  const FPS    = +QS.get('fps')   || 60;
  const LOOP   = !EXPORT && QS.get('loop') === '1';   /* play once unless asked */

  const W = 1920, H = 1080;                 /* stage space, raw pixels @1080p */
  const F200 = { w: 118,  h: 200 };
  const F400 = { w: 236,  h: 400 };
  const AT   = { w: 1073, h: 200 };
  const TOTAL = 2.2, PAUSE = 1.5;

  const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
  const ease_out = x => 1 - Math.pow(1 - x, 3);   /* ren::ease_out */
  const prog = (t, start, dur) => clamp((t - start) / dur, 0, 1);

  /* ---- timeline, straight off the state machine ---------------- */
  function state(t) {
    const toggle = t < .25 ? t / .25                     /* veil + blur fade in  */
                 : t < 2.0 ? 1                           /* hold                 */
                 : clamp(1 - (t - 2.0) / .2, 0, 1);      /* fade out, 0.2s       */
    const letter = 2 - ease_out(prog(t, .25, .5));       /* F 2x -> 1x           */
    const fly1   = 1 - ease_out(prog(t, .75, .5));       /* F slides left        */
    const fly2   = 1 - ease_out(prog(t, 1.25, .75));     /* ATALITY rises in     */
    return { t, toggle, letter, fly1, fly2, ending: t >= .75, done: t >= TOTAL };
  }

  /* ---- textures ------------------------------------------------- */
  const img = src => new Promise((res, rej) => {
    const i = new Image(); i.onload = () => res(i); i.onerror = rej; i.src = src;
  });

  function tint(src, R, G, B) {
    const c = document.createElement('canvas');
    c.width = src.width; c.height = src.height;
    const g = c.getContext('2d');
    g.drawImage(src, 0, 0);
    const id = g.getImageData(0, 0, c.width, c.height), d = id.data;
    for (let i = 0; i < d.length; i += 4) {
      d[i]     = (d[i]     * R / 255) | 0;
      d[i + 1] = (d[i + 1] * G / 255) | 0;
      d[i + 2] = (d[i + 2] * B / 255) | 0;
    }
    g.putImageData(id, 0, 0);
    return c;
  }

  /* ---- renderer ------------------------------------------------- */
  function draw(ctx, s, A) {
    ctx.setTransform(A.k, 0, 0, A.k, A.ox, A.oy);
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = 'source-over';
    ctx.clearRect(0, 0, W, H);

    /* init_anim.cpp:125 鈥?black veil, alpha = .75 * toggle */
    if (s.toggle > .001) {
      ctx.globalAlpha = .75 * s.toggle;
      ctx.fillStyle = '#000';
      ctx.fillRect(0, 0, W, H);
    }

    const cx = W * .5, cy = H * .5;

    if (!s.ending) {
      /* the letter: f_400 at size * letter * .5, alpha = 2 - letter */
      const sw = F400.w * s.letter * .5, sh = F400.h * s.letter * .5;
      ctx.globalAlpha = clamp(2 - s.letter, 0, 1);
      ctx.drawImage(A.f400, cx - sw * .5, cy - sh * .5, sw, sh);
    } else {
      /* ending: F flies left, ATALITY rises from below and fades in */
      const fa_w = F200.w * .5 + AT.w * .5;                       /* 595.5 */
      const fx = cx - F200.w * .5 - (fa_w - F200.w * .5) * (1 - s.fly1);
      ctx.globalAlpha = clamp(s.toggle, 0, 1);
      ctx.drawImage(A.f200, fx, cy - F200.h * .5, F200.w, F200.h);

      const ax = cx - (fa_w - F200.w);
      const ay = cy + AT.h * .5 + AT.h * s.fly2 - AT.h;
      ctx.globalAlpha = clamp(1 - s.fly2, 0, 1) * clamp(s.toggle, 0, 1);
      ctx.drawImage(A.at, ax, ay, AT.w, AT.h);
    }
    ctx.globalAlpha = 1;
  }

  /* ---- minimal progressive MP4 muxer (H.264 / AVCC, one chunk) --- */
  const u32 = v => new Uint8Array([(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255]);
  const u16 = v => new Uint8Array([(v >>> 8) & 255, v & 255]);
  const str8 = t => { const a = new Uint8Array(t.length); for (let i = 0; i < t.length; i++) a[i] = t.charCodeAt(i); return a; };
  const cat = list => { const n = list.reduce((a, p) => a + p.length, 0); const o = new Uint8Array(n); let k = 0;
                        for (const p of list) { o.set(p, k); k += p.length; } return o; };
  const box = (type, ...parts) => { const body = cat(parts); return cat([u32(body.length + 8), str8(type), body]); };
  const full = (type, ver, flags, ...parts) =>
    box(type, new Uint8Array([ver, (flags >>> 16) & 255, (flags >>> 8) & 255, flags & 255]), ...parts);
  const MATRIX = cat([u32(0x10000), u32(0), u32(0), u32(0), u32(0x10000), u32(0), u32(0), u32(0), u32(0x40000000)]);

  function muxMP4(samples, avcC, w, h, fps) {
    const n = samples.length;
    const durMs = Math.round(n / fps * 1000);
    const mdatBody = cat(samples.map(s => s.data));

    const avc1 = box('avc1',
      new Uint8Array(6), u16(1), u16(0), u16(0), u32(0), u32(0), u32(0),
      u16(w), u16(h), u32(0x00480000), u32(0x00480000), u32(0), u16(1),
      new Uint8Array(32), u16(0x18), new Uint8Array([0xFF, 0xFF]),
      box('avcC', avcC));

    let reordered = false;
    for (let i = 1; i < n; i++) if (samples[i].ts < samples[i - 1].ts) { reordered = true; break; }
    let ctts = new Uint8Array(0);
    if (reordered) {
      const tc = fps;
      const offs = [];
      for (let i = 0; i < n; i++) offs.push(Math.round(samples[i].ts * tc / 1e6) - i);
      const runs = [];
      for (const o of offs) {
        const last = runs[runs.length - 1];
        if (last && last[1] === o) last[0]++;
        else runs.push([1, o]);
      }
      ctts = full('ctts', 1, 0, u32(runs.length),
        ...runs.map(r => cat([u32(r[0]), u32(r[1] < 0 ? r[1] + 0x100000000 : r[1])])));
    }

    const keyIdx = samples.map((s, i) => s.key ? i + 1 : 0).filter(Boolean);
    const stss = (keyIdx.length && keyIdx.length < n)
      ? full('stss', 0, 0, u32(keyIdx.length), ...keyIdx.map(u32)) : new Uint8Array(0);
    const stsz = full('stsz', 0, 0, u32(0), u32(n), ...samples.map(s => u32(s.data.length)));
    const stbl = off => box('stbl',
      full('stsd', 0, 0, u32(1), avc1),
      full('stts', 0, 0, u32(1), u32(n), u32(1)),
      ctts, stss,
      full('stsc', 0, 0, u32(1), u32(1), u32(n), u32(1)),
      stsz,
      full('stco', 0, 0, u32(1), u32(off)));
    const minf = off => box('minf',
      full('vmhd', 0, 1, u16(0), u16(0), u16(0), u16(0)),
      box('dinf', full('dref', 0, 0, u32(1), full('url ', 0, 1))),
      stbl(off));
    const mdia = off => box('mdia',
      full('mdhd', 0, 0, u32(0), u32(0), u32(fps), u32(n), u16(0x55C4), u16(0)),
      full('hdlr', 0, 0, u32(0), str8('vide'), new Uint8Array(12), str8('VideoHandler\u0000')),
      minf(off));
    const trak = off => box('trak',
      full('tkhd', 0, 3, u32(0), u32(0), u32(1), u32(0), u32(durMs), new Uint8Array(8),
        u16(0), u16(0), u16(0), u16(0), MATRIX, u32(w << 16), u32(h << 16)),
      mdia(off));
    const mvhd = full('mvhd', 0, 0, u32(0), u32(0), u32(1000), u32(durMs),
      u32(0x00010000), u16(0x0100), u16(0), new Uint8Array(8), MATRIX, new Uint8Array(24), u32(2));

    const ftyp = box('ftyp', str8('isom'), u32(512), str8('isom'), str8('iso2'), str8('avc1'), str8('mp41'));
    const moov0 = box('moov', mvhd, trak(0));
    const off = ftyp.length + moov0.length + 8;
    const moov = box('moov', mvhd, trak(off));
    return cat([ftyp, moov, u32(mdatBody.length + 8), str8('mdat'), mdatBody]);
  }

  async function encodeVideo(fps, frames, bgHex) {
    let cfg = null;
    for (const codec of ['avc1.640028', 'avc1.4D4028']) {
      const c = { codec, width: W, height: H, bitrate: 12e6, framerate: fps,
                  avc: { format: 'avc' }, latencyMode: 'quality' };
      try { const s = await VideoEncoder.isConfigSupported(c); if (s.supported) { cfg = c; break; } } catch (e) {}
    }
    if (!cfg) throw new Error('no H.264 encoder');

    const samples = [];
    let avcC = null, err = null;
    const enc = new VideoEncoder({
      output: (chunk, meta) => {
        const data = new Uint8Array(chunk.byteLength);
        chunk.copyTo(data);
        samples.push({ data, key: chunk.type === 'key', ts: chunk.timestamp });
        if (meta && meta.decoderConfig && meta.decoderConfig.description && !avcC)
          avcC = new Uint8Array(meta.decoderConfig.description);
      },
      error: e => { err = e; }
    });
    enc.configure(cfg);

    const flat = document.createElement('canvas');
    flat.width = W; flat.height = H;
    const fctx = flat.getContext('2d', { alpha: false });

    for (let i = 0; i < frames; i++) {
      window.__renderAt(i, fps);
      let px;
      if (bgHex) {
        fctx.globalAlpha = 1;
        fctx.fillStyle = bgHex;
        fctx.fillRect(0, 0, W, H);
        fctx.drawImage(canvas, 0, 0, W, H);
        px = fctx.getImageData(0, 0, W, H);
      } else {
        px = ctx.getImageData(0, 0, W, H);
      }
      const vf = new VideoFrame(px.data, {
        format: 'RGBA', codedWidth: W, codedHeight: H,
        timestamp: Math.round(i * 1e6 / fps),
        duration: Math.round(1e6 / fps)
      });
      enc.encode(vf, { keyFrame: i % (fps | 0) === 0 });
      vf.close();
      if (enc.encodeQueueSize > 6) await new Promise(r => requestAnimationFrame(r));
    }
    await enc.flush();
    enc.close();
    if (err) throw err;
    return { bytes: muxMP4(samples, avcC, W, H, fps) };
  }

  const toB64 = bytes => {
    let s = '';
    const N = 0x8000;
    for (let i = 0; i < bytes.length; i += N)
      s += String.fromCharCode.apply(null, bytes.subarray(i, i + N));
    return btoa(s);
  };

  /* ------------------------------------------------------------------
     WebM writer (VP9). Alpha goes into BlockAdditional/BlockAddID=1, the
     standard way VP8/VP9 carry an alpha plane (Video/AlphaMode=1), because
     WebCodecs hands us pixels only 鈥?we encode the alpha plane as its own
     greyscale VP9 stream and interleave it here.
     ------------------------------------------------------------------ */
  const ebmlSize = n => {
    let len = 1;
    while (n >= Math.pow(2, 7 * len) - 1) len++;
    const b = new Uint8Array(len);
    let v = n;
    for (let i = len - 1; i >= 0; i--) { b[i] = v & 255; v = Math.floor(v / 256); }
    b[0] |= 0x80 >> (len - 1);
    return b;
  };
  const idb = hex => new Uint8Array(hex.split(' ').map(h => parseInt(h, 16)));
  const el = (hex, payload) => cat([idb(hex), ebmlSize(payload.length), payload]);
  const u8b = v => new Uint8Array([v & 255]);
  const u16b = v => new Uint8Array([(v >>> 8) & 255, v & 255]);
  const f64b = v => { const a = new ArrayBuffer(8); new DataView(a).setFloat64(0, v); return new Uint8Array(a); };
  const trackVint = n => {                      /* track number as vint */
    const b = new Uint8Array([0x80 | (n & 0x7f)]);
    return b;
  };

  function muxWebM(color, alpha, w, h, fps, durMs) {
    const ebml = el('1A 45 DF A3', cat([
      el('42 86', u8b(1)), el('42 F7', u8b(1)), el('42 F2', u8b(4)), el('42 F3', u8b(8)),
      el('42 82', str8('webm')), el('42 87', u8b(4)), el('42 85', u8b(2)),
    ]));

    const info = el('15 49 A9 66', cat([
      el('2A D7 B1', u32(1000000)),          /* TimecodeScale: 1 ms */
      el('4D 80', str8('fatality-inject')), el('57 41', str8('fatality-inject')),
      el('44 89', f64b(durMs)),
    ]));

    const tracks = el('16 54 AE 6B', el('AE', cat([
      el('D7', u8b(1)), el('73 C5', u8b(1)), el('83', u8b(1)),
      el('86', str8('V_VP9')),
      el('E0', cat([el('B0', u16b(w)), el('BA', u16b(h)), el('53 C0', u8b(1))])),
    ])));

    const blocks = [];
    for (let i = 0; i < color.length; i++) {
      const ms = Math.round(i * 1000 / fps);
      const rel = ms - 0;                    /* single cluster at 0 */
      const block = cat([trackVint(1), u16b(rel & 0xffff), u8b(0x80), color[i]]);
      const more = el('A6', cat([el('A5', alpha[i]), el('EE', u8b(1))]));
      blocks.push(el('A0', cat([el('A1', block), el('75 A1', more)])));
    }
    const cluster = el('1F 43 B6 75', cat([el('E7', u32(0)), ...blocks]));
    const segment = el('18 53 80 67', cat([info, tracks, cluster]));
    return cat([ebml, segment]);
  }

  /* colour video + alpha-as-luma video, both VP9 */
  async function encodeWebM(fps, frames, onProgress) {
    const mk = async () => {
      const cfg = { codec: 'vp09.00.10.08', width: W, height: H, bitrate: 10e6,
                    framerate: fps, latencyMode: 'quality' };
      const sup = await VideoEncoder.isConfigSupported(cfg);
      if (!sup.supported) throw new Error('no vp9 encoder');
      let err = null;
      const chunks = [];
      const enc = new VideoEncoder({
        output: c => { const d = new Uint8Array(c.byteLength); c.copyTo(d); chunks.push(d); },
        error: e => { err = e; }
      });
      enc.configure(cfg);
      return { enc, chunks, err: () => err };
    };
    const col = await mk(), alp = await mk();

    const flat = document.createElement('canvas');
    flat.width = W; flat.height = H;
    const fctx = flat.getContext('2d', { willReadFrequently: true });

    for (let i = 0; i < frames; i++) {
      window.__renderAt(i, fps);
      const src = ctx.getImageData(0, 0, W, H);
      const d = src.data;
      const rgb = new Uint8Array(d.length), lum = new Uint8Array(d.length);
      for (let p = 0; p < d.length; p += 4) {
        const a = d[p + 3];
        rgb[p] = d[p]; rgb[p + 1] = d[p + 1]; rgb[p + 2] = d[p + 2]; rgb[p + 3] = 255;
        lum[p] = lum[p + 1] = lum[p + 2] = a; lum[p + 3] = 255;
      }
      const ts = Math.round(i * 1e6 / fps), dur = Math.round(1e6 / fps);
      const f1 = new VideoFrame(rgb, { format: 'RGBA', codedWidth: W, codedHeight: H, timestamp: ts, duration: dur });
      const f2 = new VideoFrame(lum, { format: 'RGBA', codedWidth: W, codedHeight: H, timestamp: ts, duration: dur });
      col.enc.encode(f1, { keyFrame: true });
      alp.enc.encode(f2, { keyFrame: true });
      f1.close(); f2.close();
      if (col.enc.encodeQueueSize > 4) await new Promise(r => requestAnimationFrame(r));
      if (onProgress) onProgress((i + 1) / frames);
    }
    await col.enc.flush(); await alp.enc.flush();
    col.enc.close(); alp.enc.close();
    if (col.err()) throw col.err();
    if (alp.err()) throw alp.err();
    if (col.chunks.length !== alp.chunks.length)
      throw new Error('stream length mismatch ' + col.chunks.length + '/' + alp.chunks.length);
    const durMs = Math.round(frames * 1000 / fps);
    return { bytes: muxWebM(col.chunks, alp.chunks, W, H, fps, durMs) };
  }

  /* ---- boot ----------------------------------------------------- */
  const canvas = document.getElementById('cv');
  const ctx = canvas.getContext('2d', { alpha: true });
  const blurEl = document.getElementById('blur');
  let A = null, t0 = 0, playing = true, pauseUntil = 0;

  function fit() {
    const dpr = clamp(window.devicePixelRatio || 1, 1, 2);
    canvas.style.width  = '100vw';
    canvas.style.height = '100vh';
    canvas.width  = Math.round(window.innerWidth * dpr);
    canvas.height = Math.round(window.innerHeight * dpr);
    A.k = Math.max(canvas.width / W, canvas.height / H);
    A.ox = (canvas.width - W * A.k) * .5;
    A.oy = (canvas.height - H * A.k) * .5;
  }

  function frame(t) {
    const s = state(t);
    draw(ctx, s, A);
    blurEl.style.opacity = clamp(s.toggle, 0, 1).toFixed(3);
    return s;
  }

  function tick(now) {
    if (playing) {
      const t = (now - t0) / 1000;
      const s = frame(t);
      if (s.done) { if (!LOOP) { playing = false; } else { playing = false; pauseUntil = now + PAUSE * 1000; } }
    } else if (LOOP && now >= pauseUntil && pauseUntil > 0) {
      t0 = now; playing = true; pauseUntil = 0;
    }
    requestAnimationFrame(tick);
  }

  (async () => {
    const [i200, i400, iat] = await Promise.all([
      img('data:image/png;base64,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'), img('data:image/png;base64,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'), img('data:image/png;base64,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')
    ]);
    A = {
      f200: tint(i200, 235, 5, 90),
      f400: tint(i400, 235, 5, 90),
      at:   tint(iat,  235, 5, 90),
      k: 1
    };

    if (EXPORT) {
      /* deterministic frames for the offline renderer; no UI, no loop */
      document.body.classList.add('clear');          /* keep the capture transparent */
      canvas.width = W; canvas.height = H;
      A.k = 1;
      window.__renderAt = (f, fps) => {
        const s = frame(Math.min(f / (fps || FPS), TOTAL));
        return s;
      };
      window.__encode = async (fps, frames, bgHex) => {
        const r = await encodeVideo(fps, frames, bgHex || null);
        return { b64: toB64(r.bytes), size: r.bytes.length };
      };
      window.__encodeWebM = async (fps, frames) => {
        const r = await encodeWebM(fps, frames);
        return { b64: toB64(r.bytes), size: r.bytes.length };
      };
      window.__renderAt(QS.has('frame') ? FRAME : 0);
      document.title = 'ready';
      return;
    }

    fit();
    window.addEventListener('resize', fit);
    if (QS.get('clear') === '1') document.body.classList.add('clear');
    t0 = performance.now();
    requestAnimationFrame(tick);
  })();
})();
</script>
</body>
</html>
)FATALITY";
