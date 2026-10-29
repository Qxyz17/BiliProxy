"""把 third_party/ffmpeg.exe 用 zlib 压缩成 build/ffmpeg.deflate，供 objcopy 嵌进 exe。"""
import os
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'third_party', 'ffmpeg.exe')
DST = os.path.join(ROOT, 'build', 'ffmpeg.deflate')


def main():
    if not os.path.exists(SRC):
        print('missing', SRC)
        return 1
    raw_size = os.path.getsize(SRC)
    comp = zlib.compressobj(9, zlib.DEFLATED, -15)
    out = bytearray()
    with open(SRC, 'rb') as f:
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            out += comp.compress(chunk)
    out += comp.flush()
    with open(DST, 'wb') as f:
        f.write(out)
    print('raw  : %d bytes' % raw_size)
    print('defl : %d bytes (%.1f%%)' % (len(out), 100.0 * len(out) / raw_size))
    print('out  :', DST)
    return 0


if __name__ == '__main__':
    sys.exit(main())
