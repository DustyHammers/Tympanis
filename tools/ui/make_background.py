"""Makes resources/ui/TYMPANIS_Background.png, the editor's background artwork (1536 x 1024).

    python3 tools/ui/make_background.py CYMBALIS_BACKGROUND_PNG [OUT_PNG]

It is Cymbalis's background (resources/ui/CYMBALIS_Background_logo.png of Cymbalis: the light and
the vignette) with Cymbalis's title painted out, and Tympanis's title drawn in, as in the approved
mockup (docs/ui/tympanis_mockup.py, chrome()): TYMPANIS in Carlito, 339 units wide from x 66, ink
top y 97, a gradient #665E53 -> #A5A299; under it MAPLE (#4E3A28 -> #A0743E) and MODELING
INSTRUMENT (#5C5F5F), Carlito 23 with 3.6 letter spacing, ink top y 184. The tabs, the boxes, the
DHammers logo and everything else are drawn by the editor.

Needs numpy, opencv-python and pillow. The fonts are resources/ui/Carlito-Regular.ttf.
"""
import os
import sys

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(HERE, '..', '..', 'resources', 'ui')
FONT = os.path.join(UI, 'Carlito-Regular.ttf')


def hx(s):
    s = s.lstrip('#')
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def font(size):
    return ImageFont.truetype(FONT, size)


def text_w(s, size, sp):
    f = font(size)
    return sum(f.getlength(c) for c in s) + sp * (len(s) - 1)


def grad_text(img, s, x, y, size, sp, c0, c1):
    """letter-spaced text with a left-to-right gradient; y is the ink top of the capitals"""
    f = font(size)
    w = int(text_w(s, size, sp)) + 4
    hh = int(size * 1.6)
    m = Image.new('L', (w, hh), 0)
    dm = ImageDraw.Draw(m)
    xx = 0
    asc = f.getbbox('H')[1]
    for c in s:
        dm.text((xx, 0), c, font=f, fill=255)
        xx += f.getlength(c) + sp
    a = np.array(m, np.float32) / 255
    t = np.linspace(0, 1, w)[None, :, None]
    col = np.array(c0, np.float32) * (1 - t) + np.array(c1, np.float32) * t
    base = np.array(img, np.float32)
    x0, y0 = int(x), int(y - asc)
    sub = base[y0:y0 + hh, x0:x0 + w]
    aa = a[:sub.shape[0], :sub.shape[1], None]
    base[y0:y0 + hh, x0:x0 + w] = sub * (1 - aa) + np.broadcast_to(col, (hh, w, 3))[:sub.shape[0], :sub.shape[1]] * aa
    return Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(UI, 'TYMPANIS_Background.png')
    bg = cv2.imread(src)
    # paint out Cymbalis's title and subtitle
    mask = np.zeros(bg.shape[:2], np.uint8)
    mask[85:145, 55:420] = 255
    mask[175:212, 25:460] = 255
    txt = (cv2.cvtColor(bg, cv2.COLOR_BGR2GRAY) > 24).astype(np.uint8) * 255
    m = cv2.dilate(cv2.bitwise_and(mask, txt), np.ones((7, 7), np.uint8))
    bg = cv2.inpaint(bg, m, 9, cv2.INPAINT_TELEA)
    pil = Image.fromarray(cv2.cvtColor(bg, cv2.COLOR_BGR2RGB))
    sz = 55
    spc = (339 - text_w('TYMPANIS', sz, 0)) / 7
    pil = grad_text(pil, 'TYMPANIS', 66, 97, sz, spc, hx('665E53'), hx('A5A299'))
    pil = grad_text(pil, 'MAPLE', 32, 184, 23, 3.6, hx('4E3A28'), hx('A0743E'))
    x = 32 + text_w('MAPLE', 23, 3.6) + 12
    pil = grad_text(pil, 'MODELING INSTRUMENT', x, 184, 23, 3.6, hx('5C5F5F'), hx('5C5F5F'))
    pil.save(out, optimize=True)
    print(out)


if __name__ == '__main__':
    main()
