"""把 src/webui/app.js 与 app.css 打包成 src/webui_html.h。"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src', 'webui')
DST = os.path.join(ROOT, 'src', 'webui_html.h')

DELIM = 'BPWEB'


def read(p):
    with open(p, 'r', encoding='utf-8', newline='') as f:
        return f.read()


def main():
    js = read(os.path.join(SRC, 'app.js'))
    css = read(os.path.join(SRC, 'app.css'))

    for name, text, bad in (('app.js', js, '</script'), ('app.css', css, '</style')):
        if ')' + DELIM + '(' in text:
            print('%s 里出现了原始字符串分隔符 )%s(' % (name, DELIM))
            return 1
        if bad in text.lower():
            print('%s 里出现了 %s，无法内联' % (name, bad))
            return 1

    out = '// 由 tools/pack_webui.py 生成，内容来自 src/webui\n#pragma once\n\n'
    out += 'static const char* kWebUiCss = R"%s(%s)%s";\n\n' % (DELIM, css, DELIM)
    out += 'static const char* kWebUiJs = R"%s(%s)%s";\n' % (DELIM, js, DELIM)

    with open(DST, 'w', encoding='utf-8', newline='\n') as f:
        f.write(out)
    print('js  : %d bytes' % len(js))
    print('css : %d bytes' % len(css))
    print('out :', DST)
    return 0


if __name__ == '__main__':
    sys.exit(main())
