"""Makes resources/ui/TYMPANIS_Background.png, the editor's background artwork (1536 x 1024).

    python3 tools/ui/make_background.py CYMBALIS_BACKGROUND_PNG [OUT_PNG]

It is Cymbalis's background (resources/ui/CYMBALIS_Background_logo.png of Cymbalis: the light and
the vignette) with Cymbalis's title painted out, and Tympanis's title drawn in, as in the approved
mockup (docs/ui/tympanis_mockup.py, chrome()): TYMPANIS in Carlito, 339 units wide from x 66, ink
top y 97, a gradient #665E53 -> #A5A299; under it MAPLE (#4E3A28 -> #A0743E) and MODELING
INSTRUMENT (#5C5F5F), Carlito 23 with 3.6 letter spacing, ink top y 184. Since 2026-11-27 (Kris):
TYMPANIS 10 % smaller (305 wide, same middle), the subtitle 5 % smaller, both from x 66. The tabs, the boxes, the
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


def fill_smooth(img, y0, y1, x0, x1, band=14, feather=8, deg=3):
    """Replaces img[y0:y1, x0:x1] with a polynomial surface (degree deg in x and y) fitted to a
    band around the box, blended in over feather pixels at the box's edges."""
    H, W = img.shape[:2]
    Y0, Y1, X0, X1 = max(0, y0 - band), min(H, y1 + band), max(0, x0 - band), min(W, x1 + band)
    yy, xx = np.mgrid[Y0:Y1, X0:X1]
    ring = ~((yy >= y0) & (yy < y1) & (xx >= x0) & (xx < x1))
    u, v = (xx - X0) / max(1, X1 - X0), (yy - Y0) / max(1, Y1 - Y0)
    terms = [u ** i * v ** j for i in range(deg + 1) for j in range(deg + 1 - i)]
    A = np.stack([t[ring] for t in terms], 1)
    out = img.astype(np.float32).copy()
    full = np.stack(terms, -1)
    for c in range(img.shape[2]):
        coef, *_ = np.linalg.lstsq(A, img[Y0:Y1, X0:X1, c][ring].astype(np.float32), rcond=None)
        out[Y0:Y1, X0:X1, c] = np.where(ring, out[Y0:Y1, X0:X1, c], full @ coef)
    # feather: inside the box near its edge, mix the original back in
    d = np.minimum.reduce([yy - y0, y1 - 1 - yy, xx - x0, x1 - 1 - xx]).astype(np.float32)
    w = np.clip(d / feather, 0, 1)[..., None]
    inside = (~ring)[..., None]
    blend = np.where(inside, w * out[Y0:Y1, X0:X1] + (1 - w) * img[Y0:Y1, X0:X1].astype(np.float32), out[Y0:Y1, X0:X1])
    out[Y0:Y1, X0:X1] = blend
    return np.clip(out, 0, 255).astype(np.uint8)


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(UI, 'TYMPANIS_Background.png')
    bg = cv2.imread(src)
    # paint out Cymbalis's title and subtitle: each box filled with a smooth surface fitted to the
    # light in a band around it, feathered in (2026-11-27: inpainting only the text left faint
    # smudges that the smaller title no longer covers)
    for y0, y1, x0, x1 in ((80, 150, 40, 440), (170, 218, 0, 480)):
        bg = fill_smooth(bg, y0, y1, x0, x1)
    pil = Image.fromarray(cv2.cvtColor(bg, cv2.COLOR_BGR2RGB))
    # Kris, 2026-11-27: the title 10 % smaller (it keeps its middle), the subtitle 5 % smaller,
    # both left-aligned at x 66 with the tabs and the DHammers logo
    sz = 55 * 0.9
    spc = (339 * 0.9 - text_w('TYMPANIS', sz, 0)) / 7
    cap = lambda size: font(size).getbbox('H')[3] - font(size).getbbox('H')[1]
    top = 97 + (cap(55) - cap(sz)) / 2
    pil = grad_text(pil, 'TYMPANIS', 66, top, sz, spc, hx('665E53'), hx('A5A299'))
    ssz, ssp = 23 * 0.95, 3.6 * 0.95
    pil = grad_text(pil, 'MAPLE', 66, 184, ssz, ssp, hx('4E3A28'), hx('A0743E'))
    x = 66 + text_w('MAPLE', ssz, ssp) + 12 * 0.95
    pil = grad_text(pil, 'MODELING INSTRUMENT', x, 184, ssz, ssp, hx('5C5F5F'), hx('5C5F5F'))
    pil.save(out, optimize=True)
    print(out)


if __name__ == '__main__':
    main()
