"""诊断 MP4：盒结构 + 是否带 edts/ctts 负 CTS + xml 品牌 + 编解码关键参数。"""
import glob
import os
import struct
import sys

CONTAINERS = {b'moov', b'trak', b'mdia', b'minf', b'stbl', b'edts', b'dinf',
              b'mvex', b'moof', b'traf', b'udta'}

def find_file():
    if len(sys.argv) > 1:
        return sys.argv[1]
    desk = os.path.join(os.environ['USERPROFILE'], 'Desktop')
    vids = []
    for pat in ('*.mp4', '*.mkv', '*.mov', '*.webm'):
        vids += glob.glob(os.path.join(desk, pat))
    return vids[0] if vids else None

def walk(f, start, end, depth=0, out=None):
    pos = start
    while pos + 8 <= end:
        f.seek(pos)
        hdr = f.read(8)
        if len(hdr) < 8:
            break
        size = struct.unpack('>I', hdr[:4])[0]
        typ = hdr[4:8]
        hlen = 8
        if size == 1:
            size = struct.unpack('>Q', f.read(8))[0]
            hlen = 16
        elif size == 0:
            size = end - pos
        if size < hlen or pos + size > end:
            out.append('  ' * depth + 'BAD %s size=%d @%d' % (typ, size, pos))
            break
        out.append('  ' * depth + '%s size=%d @%d' % (typ.decode('latin1'), size, pos))
        if typ in CONTAINERS:
            walk(f, pos + hlen, pos + size, depth + 1, out)
        pos += size
    return out

def main():
    p = find_file()
    if not p:
        print('no video on Desktop')
        return 1
    total = os.path.getsize(p)
    print('file  :', os.path.basename(p))
    print('size  :', total, 'bytes')

    with open(p, 'rb') as f:
        lines = walk(f, 0, total, 0, [])

    for ln in lines:
        print(ln)

    # 关键子盒细节
    print('\n--- details ---')
    with open(p, 'rb') as f:
        raw = f.read()

    def find_box(name, start=0):
        i = raw.find(name, start)
        return i

    for name in (b'elst', b'ctts', b'stss', b'avcC', b'esds'):
        i = raw.find(name)
        if i < 0:
            print('%s: absent' % name.decode())
            continue
        # 盒起点 = 类型前 4 字节
        bstart = i - 4
        size = struct.unpack('>I', raw[bstart:bstart + 4])[0]
        print('%s: present, size=%d @%d' % (name.decode(), size, bstart))
        if name == b'elst':
            ver = raw[i + 4]
            cnt = struct.unpack('>I', raw[i + 8:i + 12])[0]
            print('   version=%d entries=%d' % (ver, cnt))
        if name == b'ctts':
            ver = raw[i + 4]
            cnt = struct.unpack('>I', raw[i + 8:i + 12])[0]
            print('   version=%d entries=%d' % (ver, cnt))
            # 看前几条 offset 是否为负（version=1 是有符号）
            vals = []
            for k in range(min(6, cnt)):
                off = i + 12 + k * 8
                v = struct.unpack('>I' if ver == 0 else '>i', raw[off + 4:off + 8])[0]
                vals.append(v)
            print('   first offsets:', vals)
        if name == b'avcC':
            cfgver = raw[i + 4]
            profile = raw[i + 5]
            compat = raw[i + 6]
            level = raw[i + 7]
            print('   configVersion=%d profile=0x%02X compat=0x%02X level=%d (%.1f)'
                  % (cfgver, profile, compat, level, level / 10.0))

    # ftyp 品牌
    bstart = raw.find(b'ftyp') - 4
    if bstart >= 0:
        size = struct.unpack('>I', raw[bstart:bstart + 4])[0]
        body = raw[bstart + 4:bstart + size]
        print('\nftyp: major=%s' % body[4:8].decode('latin1'))
        brands = [body[k:k + 4].decode('latin1') for k in range(8, len(body), 4)]
        print('      brands=%s' % ','.join(brands))

    # moov 是否在 mdat 之前
    mo = raw.find(b'moov')
    md = raw.find(b'mdat')
    print('\nmoov @%d  mdat @%d  -> %s' % (mo, md, 'moov first (faststart ok)' if mo < md else 'moov AFTER mdat (needs faststart)'))
    return 0

if __name__ == '__main__':
    sys.exit(main())
