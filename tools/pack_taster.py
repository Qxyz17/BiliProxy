"""把 extension/src 的 content.js 与 panel.css 打包成 src/taster_html.h。"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXT = os.path.join(ROOT, 'extension', 'src')
DST = os.path.join(ROOT, 'src', 'taster_html.h')

DELIM = 'BTASTER'


def read(p):
    with open(p, 'r', encoding='utf-8', newline='') as f:
        return f.read()


def main():
    js = read(os.path.join(EXT, 'content.js'))
    css = read(os.path.join(EXT, 'panel.css'))

    for name, text in (('content.js', js), ('panel.css', css)):
        if ')' + DELIM + '(' in text:
            print('%s 里出现了原始字符串分隔符 )%s(' % (name, DELIM))
            return 1
        if name == 'content.js' and '</script' in text.lower():
            print('content.js 里出现了 </script，无法内联')
            return 1
        if name == 'panel.css' and '</style' in text.lower():
            print('panel.css 里出现了 </style，无法内联')
            return 1

    hdr = '// 由 tools/pack_taster.py 生成，内容来自 extension/src\n#pragma once\n\n'
    hdr += 'static const char* kTasterCss = R"%s(%s)%s";\n\n' % (DELIM, css, DELIM)
    hdr += 'static const char* kTasterJs = R"%s(%s)%s";\n' % (DELIM, js, DELIM)

    with open(DST, 'w', encoding='utf-8', newline='\n') as f:
        f.write(hdr)
    print('js  : %d bytes' % len(js))
    print('css : %d bytes' % len(css))
    print('out :', DST)
    return 0


if __name__ == '__main__':
    sys.exit(main())
