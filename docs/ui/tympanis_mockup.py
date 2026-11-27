"""REFERENCE ONLY (docs/UI.md): the approved mockups, as drawn for Kris on 2026-11-24. The DRUM page
as built: VELOCITY SENSITIVITY / LEVEL / PAN at 487 / 787 / 1087. The plugin ports the wireframes in
plugin/ui/DrumModel.cpp. Paths point at a Cymbalis checkout.

Tympanis UI mockups: the DRUM page, the TUNING page and the KIT page, 1536x1024
design units, in Cymbalis's look (docs/UI_SPEC.md of Cymbalis): its background light, Carlito
labels, knob filmstrip with position arcs, box outlines, DHammers logo, and wireframes drawn the
way docs/ui/wireframe_reference.py draws the cymbals (OpenCV lines at 4x sub-pixel, BGR colours,
a soft bloom on the bright lines).

Drums are wireframes in three materials: the maple shell in British racing green lacquer (dark at
the edges, emerald where the light catches it), the coated heads in pale cream (the head mesh flexes
and lights in its membrane modes when struck), and the hardware (hoops, lugs, rods, claws, stands)
in chrome; the kicks' wood hoops in the shell's green with a silver inlay.
"""
import math, os, sys
import numpy as np
import cv2
from PIL import Image, ImageDraw, ImageFont
from scipy.special import jv, jn_zeros

CY = '/home/claude/dustyhammers/cymbalis/resources/ui/'
OUT = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/work/ui_mock/out'
os.makedirs(OUT, exist_ok=True)
W, H = 1536, 1024

# ------------------------------------------------------------------ palette (RGB hex as Cymbalis)
def hx(s): s = s.lstrip('#'); return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))
LABEL = hx('A7A7A4'); KNOBLBL = hx('A09C99'); ROWLBL = hx('695E52'); BRONZE = hx('76624C')
BOX = hx('534739'); ROWBOX = hx('463C30'); ARROW = hx('999489'); DIV = hx('353231')
POPUP = hx('141313'); OPEN = hx('A5A299'); TRACK = hx('1C1A18'); ARCD = hx('5A4A36'); ARCB = hx('ECC88C')

# wireframe colours, BGR (as the reference)
STEEL = np.array([168, 170, 174], np.float32)
STEEL_D = np.array([128, 130, 136], np.float32)
# British racing green lacquer over maple (sampled from Kris's photos, 2026-11-24): near-black
# green at the edges, deep green body, bright emerald where the light catches the lacquer
AMBER = np.array([80, 158, 64], np.float32)      # (name kept) the green body
AMBER_HI = np.array([150, 232, 128], np.float32) # emerald highlight
BURST_D = np.array([28, 60, 30], np.float32)     # near-black green at the edges
CHROME = np.array([214, 216, 220], np.float32)   # chrome hoops, lugs and claws
CHROME_D = np.array([150, 152, 156], np.float32)
HEAD = np.array([178, 204, 222], np.float32)     # coated head, pale cream
HOT = np.array([215, 238, 255], np.float32)
GOLD = np.array([140, 200, 236], np.float32)

FONT = CY + 'Carlito-Regular.ttf'
SCRIPT = CY + 'PinyonScript-Regular.ttf'
KNOB = cv2.imread(CY + 'knob.png', cv2.IMREAD_UNCHANGED)

# ------------------------------------------------------------------ background: Cymbalis's light, no title
def background():
    bg = cv2.imread(CY + 'CYMBALIS_Background_logo.png')
    mask = np.zeros(bg.shape[:2], np.uint8)
    mask[85:145, 55:420] = 255; mask[175:212, 25:460] = 255
    txt = (cv2.cvtColor(bg, cv2.COLOR_BGR2GRAY) > 24).astype(np.uint8) * 255
    m = cv2.dilate(cv2.bitwise_and(mask, txt), np.ones((7, 7), np.uint8))
    return cv2.inpaint(bg, m, 9, cv2.INPAINT_TELEA)


# ------------------------------------------------------------------ text helpers (PIL, design units)
_fonts = {}
def font(size, path=FONT):
    k = (path, size)
    if k not in _fonts: _fonts[k] = ImageFont.truetype(path, size)
    return _fonts[k]

def text_w(s, size, sp):
    f = font(size)
    return sum(f.getlength(c) for c in s) + sp * (len(s) - 1)

def text(d, s, x, y, size, sp, col, anchor='l'):
    """letter-spaced text; y is the ink top of capitals (as the spec's 'top')"""
    f = font(size); w = text_w(s, size, sp)
    if anchor == 'c': x -= w / 2
    elif anchor == 'r': x -= w
    asc = f.getbbox('H')[1]
    for c in s:
        d.text((x, y - asc), c, font=f, fill=col)
        x += f.getlength(c) + sp
    return w

def grad_text(img, s, x, y, size, sp, c0, c1, path=FONT, vertical=False):
    """text filled with a gradient (left->right, or top->bottom), drawn onto a PIL RGB image"""
    f = font(size, path)
    w = int(sum(f.getlength(c) for c in s) + sp * (len(s) - 1)) + 4
    hh = int(size * 1.6)
    m = Image.new('L', (w, hh), 0); dm = ImageDraw.Draw(m); xx = 0
    asc = f.getbbox('H')[1]
    for c in s:
        dm.text((xx, 0), c, font=f, fill=255); xx += f.getlength(c) + sp
    a = np.array(m, np.float32) / 255
    if vertical:
        ys = np.where(a.max(1) > 0)[0]; t = np.clip((np.arange(hh) - ys.min()) / max(ys.max() - ys.min(), 1), 0, 1)[:, None, None]
    else:
        t = np.linspace(0, 1, w)[None, :, None]
    col = np.array(c0, np.float32) * (1 - t) + np.array(c1, np.float32) * t
    base = np.array(img, np.float32)
    x0, y0 = int(x), int(y - asc)
    sub = base[y0:y0 + hh, x0:x0 + w]
    aa = a[:sub.shape[0], :sub.shape[1], None]
    base[y0:y0 + hh, x0:x0 + w] = sub * (1 - aa) + (col[:sub.shape[0], :sub.shape[1]] if not vertical else np.broadcast_to(col, (hh, w, 3))[:sub.shape[0], :sub.shape[1]]) * aa
    return Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))

def box(d, x, y, w, h, col=BOX, th=2, fill=None):
    if fill: d.rectangle([x, y, x + w - 1, y + h - 1], fill=fill)
    for i in range(th):
        d.rectangle([x + i, y + i, x + w - 1 - i, y + h - 1 - i], outline=col)

def arrow(d, x, y):
    d.polygon([(x, y), (x + 13, y), (x + 6.5, y + 9)], fill=ARROW)

def note_name(n):
    names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    return f'{names[n % 12]}{n // 12 - 2}'


# ------------------------------------------------------------------ knob with its position arc
def knob(img, cx, cy, p, bipolar=False):
    """img: PIL RGB. p: 0..1 (the filmstrip frame and the arc)"""
    a = np.array(img, np.float32)
    fr = int(round(p * 90)); s = KNOB[fr * 128:(fr + 1) * 128]
    rgb = s[:, :, 2::-1].astype(np.float32); al = s[:, :, 3:4].astype(np.float32) / 255
    x0, y0 = int(cx - 64), int(cy - 64)
    a[y0:y0 + 128, x0:x0 + 128] = a[y0:y0 + 128, x0:x0 + 128] * (1 - al) + rgb * al
    # arc: track, then the lit part (UI_SPEC 6)
    S = 4; lay = np.zeros((H * 1, W * 1, 3), np.float32)
    def arc(a0, a1, colf):
        steps = max(2, int(abs(a1 - a0)))
        for k in range(steps):
            t0 = a0 + (a1 - a0) * k / steps; t1 = a0 + (a1 - a0) * (k + 1) / steps
            col = colf((t0 + t1) / 2)
            pts = []
            for tt in np.linspace(t0, t1, 4):
                r = math.radians(tt - 90); pts.append((cx + 63 * math.cos(r), cy + 63 * math.sin(r)))
            pp = (np.array(pts) * S).astype(np.int32)
            cv2.polylines(lay, [pp], False, tuple(float(c) for c in col), 3, cv2.LINE_AA, shift=2)
    arc(-150, 150, lambda t: np.array(TRACK, np.float32))
    lerp = lambda u: np.array(ARCD, np.float32) * (1 - u) + np.array(ARCB, np.float32) * u
    if bipolar:
        e = 300 * (p - 0.5)
        if abs(e) > 0.5: arc(0, e, lambda t: lerp(abs(t) / 150))
    else:
        if p > 0.002: arc(-150, -150 + 300 * p, lambda t: lerp((t + 150) / 300))
    a = np.maximum(a, lay)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


# ------------------------------------------------------------------ camera
class Cam:
    def __init__(self, pos, look, F, cx, cy):
        self.p = np.array(pos, float); f = np.array(look, float) - self.p; f /= np.linalg.norm(f)
        r = np.cross(f, [0, 0, 1.0]); r /= np.linalg.norm(r); u = np.cross(r, f)
        self.f, self.r, self.u, self.F, self.cx, self.cy = f, r, u, F, cx, cy

    def proj(self, P):
        P = np.asarray(P, float); d = P - self.p
        x = d @ self.r; y = d @ self.u; z = d @ self.f
        return np.stack([self.cx + self.F * x / z, self.cy - self.F * y / z], -1), z

def orbit(target, elev_deg, dist, F, cx, cy, az_deg=0.0):
    e, z = math.radians(elev_deg), math.radians(az_deg)
    t = np.array(target, float)
    pos = t + dist * np.array([math.sin(z) * math.cos(e), -math.cos(z) * math.cos(e), math.sin(e)])
    return Cam(pos, t, F, cx, cy)

def basis(a):
    a = np.asarray(a, float); a = a / np.linalg.norm(a)
    t = np.array([0, 0, 1.0]) if abs(a[2]) < 0.9 else np.array([1.0, 0, 0])
    u = np.cross(a, t); u /= np.linalg.norm(u); v = np.cross(a, u)
    return a, u, v

def L(img, p, q, col, th=1):
    cv2.line(img, (int(p[0] * 4), int(p[1] * 4)), (int(q[0] * 4), int(q[1] * 4)), tuple(float(c) for c in col), th, cv2.LINE_AA, shift=2)

def PL(img, pts, col, th=1, closed=False):
    cv2.polylines(img, [(np.asarray(pts) * 4).astype(np.int32)], closed, tuple(float(c) for c in col), th, cv2.LINE_AA, shift=2)


# ------------------------------------------------------------------ drum wireframe
MODES = [(m, n, z) for m in range(0, 5) for n, z in enumerate(jn_zeros(m, 3))]

class Drum:
    """kind: tom, snare, kick, octo, gong. C: batter head centre (inches, world); axis: out of the
    batter head; r radius; d depth; lugs per head; two: resonant head too."""
    def __init__(self, name, kind, C, axis, r, d, lugs, two=True, key=None):
        self.name, self.kind, self.key = name, kind, key
        self.C = np.array(C, float); self.a, self.u, self.v = basis(axis)
        self.r, self.d, self.lugs, self.two = r, d, lugs, two

    def ring(self, rr, off, n=96, t0=0, t1=2 * np.pi):
        t = np.linspace(t0, t1, n)
        return self.C[None] + off * self.a[None] + rr * (np.cos(t)[:, None] * self.u + np.sin(t)[:, None] * self.v), t

    def draw(self, cam, light=np.array([0.2, -0.5, 0.85]), hit=None, glow=1.0, sel=0.0):
        """returns (layer BGR float, silhouette mask)"""
        lay = np.zeros((H, W, 3), np.float32)
        lay_side = np.zeros((H, W, 3), np.float32)     # hardware just past the silhouette: shown only outside the body
        C, a, u, v, r, d = self.C, self.a, self.u, self.v, self.r, self.d
        light = light / np.linalg.norm(light)
        def vis(P, n): return float((cam.p - P) @ n) > 0
        bright = 1.0 + 0.35 * sel
        # --- shell: a mesh like the cymbal's, on the side facing the camera: lines along the
        # axis every 5 deg and rings around it every inch, in the Starclassic amber fade (dark at
        # both edges, amber in the middle), lit from above and in front
        NS = 72 if self.kind != 'octo' else 36
        tt = np.linspace(0, 2 * np.pi, NS + 1)
        nseg = max(6, int(d * 1.2))
        cov = 0.45 if self.kind != 'kick' else 0.0       # the die-cast hoop's flange covers the shell edge
        x0_ = cov / d; x1_ = 1 - (cov / d if (self.two and self.kind not in ('octo', 'gong')) else 0.0)
        xs = np.linspace(x0_, x1_, nseg + 1)
        Pm = C[None, None] - (d * xs)[None, :, None] * a + r * (np.cos(tt)[:, None, None] * u + np.sin(tt)[:, None, None] * v)
        sp, _ = cam.proj(Pm.reshape(-1, 3)); sp = sp.reshape(NS + 1, nseg + 1, 2)
        nrm = np.cos(tt)[:, None] * u + np.sin(tt)[:, None] * v
        facing = np.array([float((cam.p - (C - 0.5 * d * a + r * nrm[i])) @ nrm[i]) > 0 for i in range(NS + 1)])
        lam = np.clip(nrm @ light, 0, 1)
        side = np.array([abs(float(nrm[i] @ cam.r)) for i in range(NS + 1)])     # 1 at the silhouette
        def shell_col(i, x):
            burst = max(0.0, 1 - abs(x - 0.45) / 0.75) ** 0.8
            c = BURST_D + (AMBER - BURST_D) * burst
            c = c + (AMBER_HI - c) * max(0.0, lam[i] - 0.55) * 1.4 * burst
            return c * (0.7 + 0.65 * lam[i]) * (1 - 0.35 * side[i] ** 4) * bright
        for i in range(NS):
            if not (facing[i] and facing[i + 1]): continue
            for k in range(nseg):
                x = (k + 0.5) / nseg
                if i % 1 == 0:
                    L(lay, sp[i, k], sp[i, k + 1], shell_col(i, x), 1)
                if k > 0 and k % 1 == 0:
                    L(lay, sp[i, k], sp[i + 1, k], shell_col(i, k / nseg) * 0.7, 1)
        # the two end edges of the shell (front half; the batter edge all round when the head faces the camera)
        for off, kcol in ((0.0, 0.8), (-d, 0.55)):
            P, t = self.ring(r, off, 97)
            sp2, _ = cam.proj(P)
            for i in range(len(t) - 1):
                n = math.cos(t[i]) * u + math.sin(t[i]) * v
                if vis(P[i], n) or (off == 0.0 and vis(C, a)):
                    L(lay, sp2[i], sp2[i + 1], (BURST_D * 1.4 * kcol + AMBER * 0.35) * bright, 1)
        # --- hoops, studied from Kris's photos (2026-11-24):
        # toms, snares, gong drum, octobans: chrome triple-flange hoops: a straight wall about 1"
        # tall standing just outside the shell edge, its top edge curled outward, its bottom flange
        # with an ear at each tension rod; the rod head and washer sit on the ear.
        # kicks: wood hoops in the shell's green, 1.5" wide, a silver inlay near the shell side,
        # chrome claws hooked over the hoop, the rods running back along the shell to the lugs.
        nlug = self.lugs
        lug_t = [2 * np.pi * (k + 0.5) / nlug for k in range(nlug)]
        ends = [(0.0, 1)] + ([(-d, -1)] if self.two and self.kind not in ('octo', 'gong') else [])

        def ring_pts(dr, h, off, sgn, n=121):
            return self.ring(r + dr, off + sgn * h, n)

        def draw_ring(dr, h, off, sgn, col, th=1, all_round=False):
            P, t = ring_pts(dr, h, off, sgn)
            sp, _ = cam.proj(P)
            for i in range(len(t) - 1):
                n = math.cos(t[i]) * u + math.sin(t[i]) * v
                if all_round or vis(P[i], n):
                    L(lay, sp[i], sp[i + 1], col * bright, th)

        def frame(t, off, sgn):
            n = math.cos(t) * u + math.sin(t) * v
            tg = -math.sin(t) * u + math.cos(t) * v
            return n, tg, sgn * a

        def chrome_col(n):
            return CHROME * (0.75 + 0.35 * max(0.0, float(n @ light)))

        def bullet_lug(t, off, sgn, y0, Lg, w=0.36, D=1.05):
            """a Starclassic lug, as in Kris's photos (IMG_6478 close-ups, 2026-11-24): its flat end
            faces the hoop and takes the rod (the swivel nut shows in it); its other end, away from
            the hoop, is round, a U seen face-on; from the side it stands out most at the flat end
            and curves back down onto the shell at the round end, like a quarter round. It sits on a
            backing plate a little larger than itself. y0: the gap from the hoop's flange to the flat
            end; Lg: its length. Returns where the rod goes in."""
            n, tg, ax = frame(t, off, sgn)                              # ax points toward this hoop
            top = C + off * a + r * n - sgn * (0.45 + y0) * a           # the flat end, on the shell
            vd = cam.p - (top - ax * 0.5 * Lg)
            f = float(vd @ n) / np.linalg.norm(vd)
            if f < -0.35: return None
            tgt = lay if f > 0.1 else lay_side
            # y: distance from the flat end (0) to the round end (Lg)
            def wy(y):
                if y > Lg - w: return math.sqrt(max(w * w - (y - (Lg - w)) ** 2, 0.0)) + 1e-6
                return w
            def zy(y):                                                  # quarter round in profile
                return D * math.sqrt(max(0.0, 1 - (y / Lg) ** 2.2))
            def P(x, y, extra=0.0):
                ww = wy(y)
                z = zy(y) * max(0.0, 1 - abs(x / ww) ** 3) ** 0.35
                return top - y * ax + x * tg + (z + 0.06 + extra) * n
            col = chrome_col(n)
            ys = np.linspace(0, Lg, 21)
            # the flat end face (an arch) and cross-sections down the lug
            for k_, y in enumerate((0.0, 0.3 * Lg, 0.6 * Lg)):
                pts = [P(x, y) for x in np.linspace(-wy(y), wy(y), 15)]
                PL(tgt, cam.proj(np.array(pts))[0], col * (1.0 if k_ == 0 else 0.7) * bright, 2 if k_ == 0 else 1)
            for fx in (-0.98, -0.5, 0.0, 0.5, 0.98):
                yy = [y for y in ys if wy(y) >= abs(fx) * w * 0.999]
                pts = [P(fx * w, y) for y in yy]
                c_ = col * (1.2 if fx == 0.0 else 0.8)
                PL(tgt, cam.proj(np.array(pts))[0], np.minimum(c_, 255) * bright, 1)
            # the plan outline where it meets the backing plate
            out = [(-wy(y), y) for y in ys] + [(wy(y), y) for y in ys[::-1]]
            PL(tgt, cam.proj(np.array([top - y * ax + x * tg + 0.06 * n for x, y in out]))[0], col * 0.7 * bright, 1, True)
            # the backing plate: a larger U, dark rim
            m = 0.13
            outp = [(-(wy(y) + m), y) for y in ys] + [(0.0, Lg + m)] + [((wy(y) + m), y) for y in ys[::-1]] + [(-(w + m), -m), ((w + m), -m)]
            pl = [top - y * ax + x * tg + 0.03 * n for x, y in out]
            outer = [(-(w + m), -m)] + [(-(wy(y) + m * (1 if y < Lg - w else (wy(y) + m) / max(wy(y), 1e-3) * 0 + 1)), y) for y in ys] \
                    + [(0.0, Lg + m)] + [((wy(y) + m), y) for y in ys[::-1]] + [((w + m), -m)]
            PL(tgt, cam.proj(np.array([top - y * ax + x * tg + 0.03 * n for x, y in outer]))[0], col * 0.45 * bright, 1, True)
            # the swivel nut in the flat end, where the rod goes in
            sw = [top + q[0] * tg + q[1] * n for q in ((-0.14, 0.45), (0.14, 0.45), (0.14, 0.8), (-0.14, 0.8))]
            PL(tgt, cam.proj(np.array(sw))[0], col * 0.9 * bright, 1, True)
            return top + 0.62 * n

        if self.kind == 'kick':
            wood = BURST_D * 0.9 + AMBER * 0.75
            for off, sgn in ends:
                face = vis(C + off * a, sgn * a)
                for dr, h, col, th in ((0.0, 0.15, wood * 0.9, 1), (0.6, 0.15, wood, 1), (0.6, 1.6, wood, 2),
                                       (0.0, 1.6, wood * 0.9, 1), (0.6, 0.35, CHROME * 0.75, 1)):
                    draw_ring(dr, h, off, sgn, col, th, all_round=(face and h > 1.0))
                for t in lug_t:
                    n, tg, ax = frame(t, off, sgn)
                    cpos = C + off * a + (r + 0.6) * n
                    vd_ = cam.p - cpos; f_ = float(vd_ @ n) / np.linalg.norm(vd_)
                    if f_ < -0.35 and not face: continue
                    tgt = lay if (f_ > 0.1 or face) else lay_side
                    # the claw over the hoop's outside face and its lip over the edge
                    claw = [cpos + q[0] * n + q[1] * ax + q[2] * tg for q in
                            ((0.15, -0.4, -0.3), (0.15, 1.3, -0.3), (-0.15, 1.75, -0.3), (-0.15, 1.75, 0.3), (0.15, 1.3, 0.3), (0.15, -0.4, 0.3))]
                    PL(tgt, cam.proj(np.array(claw))[0], chrome_col(n) * bright, 1, True)
                    # the rod from the claw back along the shell into the lug's rounded end
                    if f_ < -0.35: continue
                    end = bullet_lug(t, off, sgn, 1.7, 1.9, w=0.4, D=1.1)
                    if end is not None:
                        rp = cam.proj(np.array([cpos + 0.15 * n - 0.4 * ax, end + 0.25 * n]))[0]
                        L(tgt, rp[0], rp[1], CHROME_D * 0.9 * bright)
        else:
            # die-cast hoops (toms and snares, Kris 2026-11-24; for now also the gong drum and
            # octobans), as in the photos: a rounded top edge standing above the head, an upper band,
            # a groove, then a lower flange reaching down over the shell's edge; at each rod the
            # flange's lower edge swells down and out into an ear, the rod's washer and head on it.
            # h is measured from the head (positive away from the drum).
            NA = 241
            tt_all = np.linspace(0, 2 * np.pi, NA)
            def ear_w(tt):                     # 1 at each rod, falling to 0 between them (smooth)
                k = np.zeros_like(tt)
                for tl in lug_t:
                    dd = np.angle(np.exp(1j * (tt - tl))) * r                # arc length in inches
                    k = np.maximum(k, np.clip(1 - (dd / 0.95) ** 2, 0, 1) ** 1.5)
                return k
            ew = ear_w(tt_all)
            def hoop_line(drs, hs, off, sgn, col, th=1, all_round=False):
                drs = np.broadcast_to(drs, tt_all.shape); hs = np.broadcast_to(hs, tt_all.shape)
                n_ = np.cos(tt_all)[:, None] * u + np.sin(tt_all)[:, None] * v
                P = C[None] + (off + sgn * hs)[:, None] * a + (r + drs)[:, None] * n_
                sp, _ = cam.proj(P)
                for i in range(NA - 1):
                    if all_round or vis(P[i], n_[i]):
                        L(lay, sp[i], sp[i + 1], col * bright, th)
            # two tiers (Kris, 2026-11-24): the inner tier, nearest the head, stands higher; the
            # outer tier, lower, sticks out farther from the shell and carries the ears and rods
            for off, sgn in ends:
                face = vis(C + off * a, sgn * a)
                hoop_line(0.03, 0.52, off, sgn, CHROME * 0.85, 1, face)            # inner tier: inner edge
                hoop_line(0.13, 0.58, off, sgn, CHROME * 1.0, 2, face)             # inner tier: rounded top
                hoop_line(0.24, 0.50, off, sgn, CHROME * 0.85, 1, face)            # inner tier: outer edge
                hoop_line(0.27, 0.20, off, sgn, CHROME * 0.55, 1, face)            # the step down
                hoop_line(0.32, 0.16, off, sgn, CHROME * 0.8, 1, face)             # outer tier: top, inner
                hoop_line(0.58 + 0.30 * ew, 0.14, off, sgn, CHROME * 1.0, 2, face) # outer tier: top edge, out to the ears
                hoop_line(0.64 + 0.30 * ew, -0.10, off, sgn, CHROME * 0.55, 1)     # outer tier: face
                hoop_line(0.64 + 0.34 * ew, -0.42 - 0.30 * ew, off, sgn, CHROME * 0.9, 1)   # lower edge, swelling into the ears
                for t in lug_t:
                    n, tg, ax = frame(t, off, sgn)
                    e0 = C + off * a + (r + 0.62) * n
                    vd_ = cam.p - e0; f_ = float(vd_ @ n) / np.linalg.norm(vd_)
                    if f_ < -0.35: continue                        # ears well behind the drum stay hidden
                    tgt = lay if f_ > 0.1 else lay_side
                    cc = chrome_col(n)
                    # the washer and the rod head on the outer tier's ear
                    rc = e0 + 0.18 * n + 0.14 * ax
                    for hh, rad, k in ((0.0, 0.26, 0.7), (0.06, 0.17, 1.0), (0.34, 0.17, 1.0)):
                        tt_ = np.linspace(0, 2 * np.pi, 7 if hh > 0 else 17)
                        pts = [rc + hh * ax + rad * (math.cos(x + 0.4) * n + math.sin(x + 0.4) * tg) for x in tt_]
                        PL(tgt, cam.proj(np.array(pts))[0], cc * k * bright, 1)
                    for x in (0.4, 0.4 + np.pi / 3 * 2, 0.4 + np.pi / 3 * 4):
                        pp = rc + 0.17 * (math.cos(x) * n + math.sin(x) * tg)
                        sp_ = cam.proj(np.array([pp + 0.06 * ax, pp + 0.34 * ax]))[0]
                        L(tgt, sp_[0], sp_[1], cc * 0.9 * bright)
                    # the lug, and the threaded rod down from under the ear into its flat end
                    if self.kind == 'octo':
                        y0, Lg = 0.7, 1.3
                    else:
                        y0, Lg = 0.45, min(1.7, 0.9 + 0.1 * d)
                    end = bullet_lug(t, off, sgn, y0, Lg)
                    if end is not None:
                        rp = cam.proj(np.array([rc - 0.7 * ax, end]))[0]
                        L(tgt, rp[0], rp[1], CHROME_D * 0.95 * bright)
            if self.kind == 'octo':
                draw_ring(0.0, 0.0, -d, 1, BURST_D * 1.5 + AMBER * 0.4, 1, all_round=vis(C - d * a, -a))
        # the hardware just past the silhouette shows only where it stands out beyond the body
        Pb1, _ = self.ring(r + 0.25, 0.55, 64); Pb2, _ = self.ring(r + 0.25, -d - (0.55 if self.two else 0.0), 64)
        spb, _ = cam.proj(np.vstack([Pb1, Pb2]))
        bm = np.zeros((H, W), np.uint8)
        cv2.fillPoly(bm, [cv2.convexHull((spb * 4).astype(np.float32)).astype(np.int32)], 255, cv2.LINE_AA, shift=2)
        lay = np.maximum(lay, lay_side * (1 - bm[..., None].astype(np.float32) / 255))
        # head mesh (batter, when it faces the camera; kick: the clear batter head facing the drummer)
        if vis(C, a):
            self.head_mesh(lay, cam, C, a, u, v, r, hit, light, clear=(self.kind == 'kick'), bright=bright)
        if self.kind == 'snare':
            # the strainer on the shell's side facing the camera, and the butt end opposite
            for t, h_ in ((-0.55 * np.pi, 2.2), (0.45 * np.pi, 1.4)):
                n = math.cos(t) * u + math.sin(t) * v; tg = -math.sin(t) * u + math.cos(t) * v
                P = C - 0.5 * d * a + r * n
                if not vis(P, n): continue
                q = [P + 0.8 * tg + h_ / 2 * a + 0.5 * n, P - 0.8 * tg + h_ / 2 * a + 0.5 * n, P - 0.8 * tg - h_ / 2 * a + 0.5 * n, P + 0.8 * tg - h_ / 2 * a + 0.5 * n]
                PL(lay, cam.proj(np.array(q))[0], CHROME * bright, 1, True)
        # silhouette (convex: hull of both end rings with the hoops)
        P1, _ = self.ring(r + 0.9, 0.6, 64); P2, _ = self.ring(r + 0.9, -d - 0.6, 64)
        sp, _ = cam.proj(np.vstack([P1, P2]))
        mask = np.zeros((H, W), np.uint8)
        hull = cv2.convexHull((sp * 4).astype(np.float32)).astype(np.int32)
        cv2.fillPoly(mask, [hull], 255, cv2.LINE_AA, shift=2)
        return lay, mask.astype(np.float32) / 255

    def head_mesh(self, lay, cam, C, a, u, v, r, hit, light, clear=False, bright=1.0):
        rings = np.concatenate([np.linspace(0.04, 0.965, 24), [1.0]])
        NT = 96
        th = np.linspace(0, 2 * np.pi, NT, endpoint=False)
        RR, TT = np.meshgrid(rings, th, indexing='ij')
        disp = np.zeros_like(RR); g = np.zeros_like(RR)
        if hit is not None:
            age, vel, rpos, ang = hit
            for (m, n_, z) in MODES:
                w = abs(jv(m, z * rpos)) * math.exp(-(m + 1.6 * n_) / (1.5 + 4 * vel))
                if w < 0.04: continue
                tau = 0.15 + 0.6 / (1 + m + 2 * n_)
                amp = vel * w * math.exp(-age / tau) * (1 - math.exp(-age / 0.01))
                s = jv(m, z * RR) * np.cos(m * (TT - ang)); s /= max(np.abs(s).max(), 1e-6)
                disp += amp * s * math.cos(2 * np.pi * (1.5 + 0.7 * (m + n_)) * age)
            ad = np.abs(disp); mx = ad.max()
            g = (ad / max(mx, 1e-6)) ** 1.6 * min(1.0, mx / 0.3) ** 0.5
        P = C[None, None] + r * RR[..., None] * (np.cos(TT)[..., None] * u + np.sin(TT)[..., None] * v) + (0.35 * r * disp / 7)[..., None] * a
        sp, _ = cam.proj(P.reshape(-1, 3)); sp = sp.reshape(len(rings), NT, 2)
        ys = sp[..., 1]; ytop, ybot = ys.min(), ys.max()
        def col(i, j, i2, j2):
            ytl = 1 - (0.5 * (ys[i, j] + ys[i2, j2]) - ytop) / max(ybot - ytop, 1)
            base = HEAD * ((0.14 if clear else 0.22) + (0.16 if clear else 0.34) * ytl) * bright
            gg = 0.5 * (g[i, j] + g[i2, j2])
            return base + (HOT - base) * gg
        for i in range(len(rings)):
            th_ = 2 if i == len(rings) - 1 else 1
            for j in range(NT):
                j2 = (j + 1) % NT
                L(lay, sp[i, j], sp[i, j2], col(i, j, i, j2), 1)
        for j in range(0, NT, 2):
            for i in range(len(rings) - 1):
                L(lay, sp[i, j], sp[i + 1, j], col(i, j, i + 1, j), 1)
        # the dot (Starclassic heads: a small centre dot ring)
        if hit is not None:
            age, vel, rpos, ang = hit
            T_ = 0.25 + 0.25 * vel
            if age < T_:
                q = C + r * rpos * (math.cos(ang) * u + math.sin(ang) * v)
                hp = cam.proj(q[None])[0][0]
                kk = (1 - age / T_) ** 2; rad = int((8 + 18 * vel) * (1 - 0.3 * age / T_))
                spot = np.zeros_like(lay); cv2.circle(spot, (int(hp[0]), int(hp[1])), rad, (230, 245, 255), -1, cv2.LINE_AA)
                lay += cv2.GaussianBlur(spot, (0, 0), rad * 0.6) * kk * (0.4 + 0.6 * vel)


def stand(lay, cam, top, foot_r, h_floor=0.0, legs=3, col=None, rot=0.5):
    """a stand: a pole from below `top` to the floor and its tripod legs"""
    col = CHROME_D * 0.5 if col is None else col
    top = np.array(top, float); base = np.array([top[0], top[1], 7.0])
    sp = cam.proj(np.array([top, base]))[0]; L(lay, sp[0], sp[1], col)
    for k in range(legs):
        t = rot + 2 * np.pi * k / legs
        f = np.array([top[0] + foot_r * math.cos(t), top[1] + foot_r * math.sin(t), h_floor])
        s2 = cam.proj(np.array([base, f]))[0]; L(lay, s2[0], s2[1], col)


def composite(img, items):
    """items: list of (depth, layer, mask); far first; each hides what is behind it"""
    for _, lay, mask in sorted(items, key=lambda t: -t[0]):
        if mask is not None:
            img = img * (1 - 0.93 * mask[..., None])
        img = np.maximum(img, lay)
    return img

def bloom(layer):
    b = cv2.GaussianBlur(layer, (0, 0), 3.0)
    return np.maximum(layer, 0) + 0.5 * b * (layer.max(axis=2, keepdims=True) > 140)


# ------------------------------------------------------------------ editor chrome shared by the pages
ROWS = {  # per drum: its rows (articulation, default note) — tympanis plugin/KitMap.h
    'snare2': [('HEAD', 24), ('RIM SHOT', 25), ('SIDE STICK', 26)],
    'tom3': [('HEAD', 47), ('RIM SHOT', 74)],
}

def chrome(pil, page, drum_name, channel=10):
    d = ImageDraw.Draw(pil)
    # title and subtitle (drawn into the background artwork, as Cymbalis's)
    sz = 55; spc = (339 - text_w('TYMPANIS', sz, 0)) / 7
    pil = grad_text(pil, 'TYMPANIS', 66, 97, sz, spc, hx('665E53'), hx('A5A299'))
    pil = grad_text(pil, 'MAPLE', 32, 184, 23, 3.6, hx('4E3A28'), hx('A0743E'))
    d = ImageDraw.Draw(pil)
    x = 32 + text_w('MAPLE', 23, 3.6) + 12
    text(d, 'MODELING INSTRUMENT', x, 184, 23, 3.6, hx('5C5F5F'))
    # page tabs
    tabs = ['DRUM', 'KIT', 'TUNING']; tx = 66
    for t in tabs:
        on = t == page
        w = text(d, t, tx, 258, 17.1, 3.3, LABEL if on else ROWLBL)
        if on: d.rectangle([tx, 283, tx + w, 284], fill=BRONZE)
        tx += w + 42
    # MIDI CHANNEL (top right, as Cymbalis layout v3)
    text(d, 'MIDI CHANNEL', 1384, 41, 17.1, 3.11, LABEL, 'r')
    box(d, 1404, 30, 110, 36); text(d, str(channel), 1416, 41, 17.1, 3.2, LABEL); arrow(d, 1484, 44)
    # DRUM box (not on the KIT page: there a click on a drum picks it)
    if page != 'KIT':
        text(d, 'DRUM', 199.5, 348, 17.1, 3.45, LABEL, 'c')
        box(d, 61, 371, 277, 62); text(d, drum_name, 186, 395, 17.1, 3.2, LABEL, 'c'); arrow(d, 306, 398)
    # DHammers logo (Pinyon Script, em 48, gradient, centred 199.5,950)
    f = font(48, SCRIPT); bb = f.getbbox('DHammers')
    lw, lh = bb[2] - bb[0], bb[3] - bb[1]
    pil = grad_text(pil, 'DHammers', 199.5 - lw / 2 - bb[0], 950 - lh / 2 - (bb[1] - f.getbbox('H')[1]), 48, 0, hx('8E7550'), hx('4A3D2D'), SCRIPT, vertical=True)
    return pil


def to_pil(bgr):
    return Image.fromarray(cv2.cvtColor(np.clip(bgr, 0, 255).astype(np.uint8), cv2.COLOR_BGR2RGB))


# ------------------------------------------------------------------ page 1: DRUM
def drum_page(key, drum, cam, rows, hit=None, lit_row=None, knobs=None, spec=''):
    bg = background().astype(np.float32)
    lay, mask = drum.draw(cam, hit=hit)
    img = np.maximum(bg * (1 - 0.0 * mask[..., None]), bloom(lay))
    pil = chrome(to_pil(img), 'DRUM', drum.name)
    d = ImageDraw.Draw(pil)
    text(d, spec, 199.5, 452, 15.6, 2.4, ROWLBL, 'c')
    # dividers
    d.line([(39, 757), (1161, 757)], fill=DIV); d.line([(1188, 110), (1188, 850)], fill=DIV)
    # right column: articulation rows
    text(d, 'ARTICULATION', 1201, 98, 17.1, 3.29, LABEL); text(d, 'MIDI NOTE', 1424, 98, 17.1, 3.0, LABEL)
    for k, (name, note) in enumerate(rows):
        y = 140 + 80 * k; lit = lit_row == k
        text(d, name, 1201, y + 8, 15.6, 3.0, OPEN if lit else ROWLBL)
        box(d, 1424, y, 66, 25, BRONZE if lit else ROWBOX)
        nm = note_name(note); w1 = text_w(nm, 15.6, 1.2); w2 = text_w(str(note), 15.6, 1.2)
        x0 = 1457 - (w1 + 6 + w2) / 2
        text(d, nm, x0, y + 7, 15.6, 1.2, LABEL); text(d, str(note), x0 + w1 + 6, y + 7, 15.6, 1.2, ROWLBL)
    # knobs: bottom row (this drum) and right column
    K = knobs
    for (cx, cy, lbl, p, bip, ends) in K:
        text(d, lbl, cx, cy - 97, 17.1, 3.3, KNOBLBL, 'c')
        pil = knob(pil, cx, cy, p, bip); d = ImageDraw.Draw(pil)
        if ends:
            text(d, ends[0], cx - 73, cy + 77, 15.6, 2.0, ROWLBL, 'c'); text(d, ends[1], cx + 73, cy + 77, 15.6, 2.0, ROWLBL, 'c')
    return pil


# ------------------------------------------------------------------ page 2: TUNING
def slider_row(d, y, label, lo, hi, val, txt, changed=False, ends=None, centre=None):
    x0, x1 = 640, 1236
    if changed: d.rectangle([444, y - 4, 447, y + 22], fill=BRONZE)
    text(d, label, 460, y + 4, 15.6, 2.6, LABEL)
    d.rounded_rectangle([x0, y + 7, x1, y + 11], 2, fill=ROWBOX)
    p = (val - lo) / (hi - lo); xp = x0 + p * (x1 - x0)
    if centre is not None:
        xc = x0 + (centre - lo) / (hi - lo) * (x1 - x0)
        d.rounded_rectangle([min(xc, xp), y + 7, max(xc, xp), y + 11], 2, fill=BRONZE)
        d.line([(xc, y + 3), (xc, y + 15)], fill=ROWLBL)
    else:
        d.rounded_rectangle([x0, y + 7, xp, y + 11], 2, fill=BRONZE)
    d.ellipse([xp - 7, y + 2, xp + 7, y + 16], fill=OPEN)
    if ends:
        text(d, ends[0], x0, y + 24, 12.5, 1.6, ROWLBL); text(d, ends[1], x1, y + 24, 12.5, 1.6, ROWLBL, 'r')
    box(d, 1256, y - 3, 104, 24, ROWBOX, 1, POPUP); text(d, txt, 1350, y + 4, 15.6, 1.2, LABEL, 'r')
    # reset arrow (to the default)
    cx, cy = 1384, y + 9
    d.arc([cx - 8, cy - 8, cx + 8, cy + 8], 200, 520 - 50, fill=BRONZE if changed else ROWLBL, width=2)
    d.polygon([(cx + 3, cy - 12), (cx + 9, cy - 8), (cx + 3, cy - 3)], fill=BRONZE if changed else ROWLBL)

def band(d, y, title, note):
    d.rectangle([440, y, 1500, y + 34], fill=POPUP)
    d.line([(440, y + 34), (1500, y + 34)], fill=BRONZE, width=2)
    w = text(d, title, 458, y + 10, 17.1, 3.3, LABEL)
    text(d, note, 458 + w + 22, y + 12, 14, 1.6, ROWLBL)

def tuning_page(drum, cam_small):
    bg = background().astype(np.float32)
    lay, _ = drum.draw(cam_small)
    img = np.maximum(bg, bloom(lay))
    pil = chrome(to_pil(img), 'TUNING', drum.name)
    d = ImageDraw.Draw(pil)
    text(d, '13" × 10"  ·  6 LUGS', 199.5, 452, 15.6, 2.4, ROWLBL, 'c')
    band(d, 100, 'TOM 3', 'affects only this drum')
    rows = [('TUNE', -2, 2, 0.0, '+0.00 st', False, None, 0.0),
            ('LUG DETUNE', 0, 1, 0.35, '0.35', True, ('EVEN', '5 % RMS'), None),
            ('DECAY', 0.25, 4, 1.4, '1.40 x', True, ('SHORT', 'LONG'), None),
            ('STRIKE POSITION', 0, 0.95, 0.35, '0.35', False, ('CENTER', 'RIM'), None),
            ('SIZE', 6.5, 26, 13.0, '13.0 in', False, ('6.5"', '26"'), None),
            ('LEVEL', -60, 6, 0.0, '0.0 dB', False, None, None),
            ('PAN', -1, 1, 0.0, 'C', False, ('L', 'R'), 0.0)]
    y = 160
    for r in rows:
        # the decay slider is skewed (1 x in the middle): show its position, not its value
        lbl, lo, hi, val, txt, ch, ends, cen = r
        if lbl == 'DECAY': val = lo + (hi - lo) * (math.log(val / 0.25) / math.log(16))
        if lbl == 'SIZE': val = 6.5 + (26 - 6.5) * (math.log(13 / 6.5) / math.log(4))
        if lbl == 'LEVEL': val = -60 + 66 * 0.88
        slider_row(d, y, lbl, lo, hi, val, txt, ch, ends, cen)
        y += 62
    band(d, 620, 'WHOLE KIT', 'affects every drum')
    slider_row(d, 680, 'SNARE BUZZ', 0, 1, 0.5, '0.50', False, ('OFF', 'FULL'))
    slider_row(d, 742, 'MAIN VOLUME', -60, 6, -60 + 66 * 0.88, '0.0 dB', False)
    # footer buttons, as the Cymbalis Tuning window
    for (x, t) in ((1124, 'RESET DRUM'), (1300, 'RESET ALL')):
        box(d, x, 830, 160, 34, BOX, 2); text(d, t, x + 80, 841, 15.6, 2.6, LABEL, 'c')
    text(d, 'MODIFIED', 460, 841, 15.6, 2.6, BRONZE)
    return pil


# ------------------------------------------------------------------ page 3: KIT
def kit_drums():
    """the kit set up as in Kris's reference picture, seen from the drummer's side (inches; x to
    the right, y away from the camera, z up). Rack toms in a row over the two kicks, tom 5 angled
    at the right end, tom 6 (18") the floor tom at the front right, three snares in front, the
    fourth at the left by the octobans, the gong drum at the far right."""
    D = []
    lean = lambda f, s=0.0: np.array([s, -f, 1.0])          # head normal leaning toward the camera
    # the two kicks in a V (Kris, 2026-11-24): batter heads close together at the pedals, the
    # front ends spreading apart
    vk = math.radians(20)
    D.append(Drum('Kick 1', 'kick', (-13.8, 7, 11.5), (math.sin(vk), -math.cos(vk), 0), 11, 18, 10, key='kick1'))
    D.append(Drum('Kick 2', 'kick', (13.8, 7, 11.5), (-math.sin(vk), -math.cos(vk), 0), 11, 18, 10, key='kick2'))
    # snares (Kris, 2026-11-24): the main snare in the centre is Snare 1, and Snare 2 is the same
    # drum with its wires off (one drum drawn for both); Snare 4, right of it, is a 10" snare;
    # Snare 3 at the left
    D.append(Drum('Snare 1 · 2', 'snare', (0, -14, 25), lean(0.1), 7, 6.5, 10, key='snare1'))
    # Snare 4 (10") at the left; Snare 3 not drawn for now (still playable) (Kris, 2026-11-24)
    D.append(Drum('Snare 4', 'snare', (-27, -12, 25), lean(0.12, 0.08), 5, 5.0, 8, key='snare4'))
    # rack toms resting low over the kicks, tom 5 angled at the right end, tom 6 the floor tom
    # the upper four toms spread evenly across both kicks (Kris, 2026-11-24)
    D.append(Drum('Tom 1', 'tom', (-24.5, 14.5, 33.5), lean(0.45, 0.16), 5, 10, 6, key='tom1'))  # 10" x 10" (Kris, 2026-11-25), head level with the others
    D.append(Drum('Tom 2', 'tom', (-9.5, 16, 34.5), lean(0.45, 0.05), 6, 9, 6, key='tom2'))
    D.append(Drum('Tom 3', 'tom', (6.5, 16, 35.5), lean(0.45, -0.05), 6.5, 10, 6, key='tom3'))
    D.append(Drum('Tom 4', 'tom', (23, 14.5, 36.5), lean(0.45, -0.16), 7, 11, 6, key='tom4'))
    # two floor toms side by side at the right, in a row coming toward the camera: the 16" by the
    # kick, the 18" in front of it; each turned slightly toward the main snare
    D.append(Drum('Tom 5', 'floor', (27.5, -1, 20.5), np.array([-0.12, -0.08, 1.0]), 8, 16, 8, key='tom5'))
    D.append(Drum('Tom 6', 'floor', (37.5, -18, 21.5), np.array([-0.14, -0.06, 1.0]), 9, 16, 8, key='tom6'))
    # the gong drum and the octobans: not drawn for now (still playable) (Kris, 2026-11-24)
    return D

def kick_pedal(lay, cam, x, y_bat):
    """a bass drum pedal on the floor at the batter head (steel wireframe)"""
    c = CHROME * 0.8; dm = CHROME_D * 0.5
    pts = lambda P: cam.proj(np.array(P, float))[0]
    fb = pts([(x - 2.0, y_bat - 11, 0.4), (x + 2.0, y_bat - 11, 0.4), (x + 2.0, y_bat - 1.5, 3.3), (x - 2.0, y_bat - 1.5, 3.3)])
    PL(lay, fb, c, 1, True)
    base = pts([(x - 3.2, y_bat - 12, 0), (x + 3.2, y_bat - 12, 0), (x + 3.2, y_bat, 0), (x - 3.2, y_bat, 0)])
    PL(lay, base, dm, 1, True)
    for sx in (-2.8, 2.8):
        p = pts([(x + sx, y_bat - 0.5, 0), (x + sx, y_bat - 0.5, 9)]); L(lay, p[0], p[1], c)
    p = pts([(x, y_bat - 0.5, 8.5), (x, y_bat - 2.5, 14), (x, y_bat - 1.6, 14.6)])
    L(lay, p[0], p[1], c); L(lay, p[1], p[2], c)       # beater shaft and head

def kit_page(selected='tom3', hit_key=None, hit=None):
    bg = background().astype(np.float32)
    cam = Cam((11, -128, 58), (11, 4, 22), 1480, 800, 575)
    # faint floor rug outline
    floor = np.zeros((H, W, 3), np.float32)
    # no rug (Kris, 2026-11-24)
    img = np.maximum(bg, floor)
    items = []
    for dr in kit_drums():
        sel = 1.0 if dr.key == selected else 0.0
        h = hit if dr.key == hit_key else None
        lay, mask = dr.draw(cam, hit=h, sel=sel)
        st = None              # no pedals or stands drawn (Kris, 2026-11-24)
        depth = float((dr.C - dr.d * 0.5 * dr.a - cam.p) @ cam.f)
        if dr.key in ('tom1', 'tom2', 'tom3', 'tom4'):
            depth -= 25.0          # resting on the kicks: in front of the kick shells they sit on
        items.append((depth, lay, mask))
    img = composite(img, items)
    img = np.maximum(bg, bloom(img - bg * 0) * 1.0)
    pil = chrome(to_pil(img), 'KIT', '')
    d = ImageDraw.Draw(pil)
    # the selected drum's name under the tabs, and a hover label at a drum
    text(d, 'SELECTED', 1514, 104, 15.6, 3.0, ROWLBL, 'r')
    text(d, 'TOM 3  ·  13"', 1514, 128, 17.1, 3.2, LABEL, 'r')
    return pil, cam


if __name__ == '__main__':
    # DRUM page: TOM 3 at rest, and SNARE 2 just struck (its head row lit)
    tom3 = Drum('TOM 3  ·  13"', 'tom', (0, 0, 0), (0, 0, 1), 6.5, 10, 6)
    cam = orbit((0, 0, -5.0), 34, 70, 2150, 787, 390, az_deg=-8)
    # DRUM page knobs (Kris, 2026-11-24): Tune and Decay live on the TUNING page; Strike Position
    # takes Decay's place in the right column (hidden on the kicks, which have none)
    kn = [(487, 881, 'VELOCITY SENSITIVITY', 1.0, False, ('−', '+')),
          (787, 881, 'LEVEL', 0.88, False, ('−', '+')), (1087, 881, 'PAN', 0.5, True, ('L', 'R')),
          (1349, 642, 'STRIKE POSITION', 0.35 / 0.95, False, ('CENTER', 'RIM')),
          (1350, 881, 'MAIN VOLUME', 0.88, False, ('−', '+'))]
    drum_page('tom3', tom3, cam, ROWS['tom3'], knobs=kn, spec='13" × 10"  ·  6 LUGS').save(f'{OUT}/01_drum_page_tom3.png')
    sn = Drum('SNARE 2  ·  14"', 'snare', (0, 0, 0), (0, 0, 1), 7, 6.5, 10)
    cam2 = orbit((0, 0, -3.2), 36, 70, 2250, 787, 390, az_deg=-8)
    kn2 = [(487, 881, 'VELOCITY SENSITIVITY', 1.0, False, ('−', '+')),
           (787, 881, 'LEVEL', 0.88, False, ('−', '+')), (1087, 881, 'PAN', 0.45, True, ('L', 'R')),
           (1349, 642, 'STRIKE POSITION', 0.35 / 0.95, False, ('CENTER', 'RIM')),
           (1350, 881, 'MAIN VOLUME', 0.88, False, ('−', '+'))]
    drum_page('snare2', sn, cam2, ROWS['snare2'], hit=(0.05, 1.0, 0.42, math.radians(-55)), lit_row=0, knobs=kn2,
              spec='14" × 6.5"  ·  10 LUGS').save(f'{OUT}/02_drum_page_snare2_hit.png')
    # TUNING page (TOM 3), a small wireframe of the drum in the left column
    camS = orbit((0, 0, -5.0), 34, 70, 900, 199.5, 600, az_deg=-8)
    tuning_page(tom3, camS).save(f'{OUT}/03_tuning_page_tom3.png')
    # KIT page, TOM 3 selected, SNARE 1 just struck
    kit_page('tom3', 'snare1', (0.05, 0.9, 0.35, math.radians(-50)))[0].save(f'{OUT}/04_kit_page.png')
    print('ok')
