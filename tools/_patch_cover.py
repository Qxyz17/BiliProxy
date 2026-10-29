import io
import os
import re

p = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src", "fatality_html.h")
s = io.open(p, encoding="utf-8").read()

old_fit = """  function fit() {
    const dpr = clamp(window.devicePixelRatio || 1, 1, 2);
    const s = Math.min(window.innerWidth / W, window.innerHeight / H);
    canvas.style.width  = (W * s) + 'px';
    canvas.style.height = (H * s) + 'px';
    canvas.width  = Math.round(W * dpr);
    canvas.height = Math.round(H * dpr);
    A.k = canvas.width / W;
  }"""

new_fit = """  function fit() {
    const dpr = clamp(window.devicePixelRatio || 1, 1, 2);
    canvas.style.width  = '100vw';
    canvas.style.height = '100vh';
    canvas.width  = Math.round(window.innerWidth * dpr);
    canvas.height = Math.round(window.innerHeight * dpr);
    A.k = Math.max(canvas.width / W, canvas.height / H);
    A.ox = (canvas.width - W * A.k) * .5;
    A.oy = (canvas.height - H * A.k) * .5;
  }"""

assert old_fit in s, "fit not found"
s = s.replace(old_fit, new_fit)

old_tr = "    ctx.setTransform(A.k, 0, 0, A.k, 0, 0);"
new_tr = "    ctx.setTransform(A.k, 0, 0, A.k, A.ox, A.oy);"
assert old_tr in s, "transform not found"
s = s.replace(old_tr, new_tr)

io.open(p, "w", encoding="utf-8", newline="").write(s)
print("patched fit + transform")
