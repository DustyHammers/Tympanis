#!/usr/bin/env python3
"""Bake the kit's modal models into the data the C++ engine reads (resources/bake/*.bake.json).

The C++ engine has no Bessel functions and no numpy random generator, so everything that
the Python engine (prototype/engine.py) derives from those is computed here, once:

  - the strike-position gain per slot on a radius grid (head and rim articulations, not kicks)
  - the squared mode shape per slot on the same grid (re-strike damping)
  - the uneven-lug-tension tables (which slots get a twin, split size, eigen-angle, strike angle)
  - the noise-band level corrections (the Python engine measures each band filter's gain on a
    fixed noise sequence; the ratio to the exact filter gain is baked so the C++ engine keeps
    the same band levels at any sample rate)
  - the sympathetic snare-head bank (snares)

The model data itself (layers, noise, modelled transients) is copied unchanged.

Usage: python3 tools/bake_kit.py            (writes resources/bake/)
       python3 tools/bake_kit.py --check    (fails if the committed bake differs)
"""
import argparse, json, os, sys
import numpy as np
from scipy.signal import butter, sosfilt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'prototype'))
import engine as E  # noqa: E402

# Kit order: this is the drum index order used by the C++ kit (and the snare-buzz order).
KIT = [
    ('kick1',   'KICK_705_SPkick2_DRY7S'),
    ('kick2',   'KICK_707_SPkick2_DRY7S'),
    ('snare1',  'SNRE_720_SPsnare1_shared'),
    ('snare2',  'SNRE_722_SPsnare2_shared'),
    ('snare3',  'SNRE_724_SPsnare3_shared'),
    ('snare4',  'SNRE_725_SPsnare4_shared'),
    ('tom1',    'TOM_729_SPtom1_DRY7S'),
    ('tom2',    'TOM_730_SPtom2_DRY7S'),
    ('tom3',    'TOM_731_SPtom3_DRY7S'),
    ('tom4',    'TOM_732_SPtom4_DRY7S'),
    ('tom5',    'TOM_733_SPtom5_DRY7S'),
    ('tom6',    'TOM_734_SPtom6_DRY7S'),
    ('gong',    'TOM_735_SPgongtom1_DRY7S'),
    ('octo1',   'TOM_736_SPoctoban1_DRY7S'),
    ('octo2',   'TOM_737_SPoctoban2_DRY7S'),
    ('octo3',   'TOM_738_SPoctoban3_DRY7S'),
    ('octo4',   'TOM_739_SPoctoban4_DRY7S'),
]
GRID_STEP = 0.01          # radius grid r = i / 100, i = 0 .. 95 (0 = centre, 0.95 = max strike_pos)
GRID_N = 96
SR = E.SR


def exact_norm(sos):
    """1 / RMS gain of the filter for unit white noise: 1 / sqrt(sum h^2)."""
    x = np.zeros(1 << 17); x[0] = 1.0
    h = sosfilt(sos, x)
    return 1.0 / np.sqrt(np.sum(h * h))


def shape2_fast(d, r, cache):
    """prototype/engine.py ModalDrum._shape2(r), with the per-mode rim normalisers computed once."""
    from scipy.special import jn
    if 'tab' not in cache:
        rr = np.linspace(0, 1, 200)
        cache['tab'] = np.array([np.max(jn(m_, j_ * rr) ** 2) for m_, j_ in zip(d._tab_m, d._tab_j)])
        cache['lo'] = {k: np.max(jn(d.mode_m[k], d.mode_j[k] * rr) ** 2) for k in np.where(d.mode_kind == 1)[0]}
    w = np.ones(len(d.mode_kind))
    mm = d.mode_kind == 0; w[mm] = jn(0, 2.4048 * r) ** 2
    for k in np.where(d.mode_kind == 1)[0]:
        w[k] = jn(d.mode_m[k], d.mode_j[k] * r) ** 2 / cache['lo'][k]
    vals = jn(d._tab_m, d._tab_j * r) ** 2 / cache['tab']
    for k in np.where(d.mode_kind == 2)[0]:
        ix = d.dense_idx[k]
        if ix is None or len(ix) == 0: w[k] = 0.3; continue
        w[k] = np.mean(vals[ix])
    return np.clip(w, 0, 1)


def fl(a):
    return [float(v) for v in np.asarray(a, dtype=float).ravel()]


def layer_out(l):
    tr = l.get('transient') or {}
    return {
        'freq': fl(l['freq']), 'decay': fl(l['decay']), 'amp': fl(l['amp']), 'phase': fl(l['phase']),
        'glide_g': float(l['glide_g']), 'glide_tau': float(l['glide_tau']),
        'glide_g2': float(l.get('glide_g2', 0.0)), 'glide_tau2': float(l.get('glide_tau2', 1.0)),
        'glide_w': fl(l['glide_w']) if 'glide_w' in l else None,
        'noise_peak': [float(b['peak_rms']) for b in l['noise']],
        'noise_decay': [float(b['decay']) for b in l['noise']],
        'tr_modes': [fl(r) for r in tr.get('modes', [])],          # rows: f, d, a, phase
        'tr_pulses': [fl(r) for r in tr.get('pulses', [])],        # rows: t0, fpeak, amp
        'tr_burst': [fl(r) for r in tr.get('burst', [])],          # rows per burst band: peak, t_peak, decay
    }


def bake_drum(key, model):
    path = os.path.join(ROOT, 'resources', 'models', model + '.json')
    d = E.ModalDrum(path, restrike_damp=True, variation=False)
    m = d.m
    arts = m.get('articulations') or {'head': {'layers': m['layers']}}
    for a, v in arts.items():
        if float(v.get('click_gain', 0.0) or 0.0) != 0.0:
            raise SystemExit(f'{model}: {a} click_gain != 0 is not supported by the C++ engine')
    K = len(d.mode_kind)

    # strike-position gain grids, per articulation that uses one (head and rim; never side sticks
    # or bass drums): _strike_gain uses the articulation's own amplitudes and decays
    gain_grid = {}
    if not d.is_kick:
        for art in arts:
            if art == 'side': continue
            d._use_art(art)
            rows = []
            for i in range(GRID_N):
                d.strike_pos = i / 100
                g = d._strike_gain()
                rows.append(fl(np.broadcast_to(np.asarray(g, float), (K,))))
            gain_grid[art] = rows
        d.strike_pos = d.r0
    d._use_art('head')
    cache = {}
    shape_grid = [fl(shape2_fast(d, i / 100, cache)) for i in range(GRID_N)]
    for r in (0.05, 0.35, 0.55, 0.9):        # the radii the engine actually uses: must equal the prototype's
        ref = d._shape2(r); got = shape2_fast(d, r, cache)
        if np.max(np.abs(ref - got)) > 1e-12: raise SystemExit(f'{model}: shape2 mismatch at r={r}')

    corr = [float(d.bnorm[j] / exact_norm(s)) for j, s in enumerate(d.sos)]

    out = {
        'key': key, 'model': model, 'K': K,
        'kind': 'kick' if d.is_kick else ('snare' if model.startswith('SNRE') else ('octoban' if d.is_octoban else 'tom')),
        'native_in': float(d.D0), 'f_main': float(d.f_main), 'fref': float(d.fref), 'r0': float(d.r0),
        'glide_gmax': float(d.gmax),
        'bands': [[float(lo), float(hi)] for lo, hi in d.bands],
        'band_norm_corr': corr,
        'articulations': {a: [layer_out(l) for l in v['layers']] for a, v in arts.items()},
        'lug': {
            'count': int(next((v for k, v in d.LUGS.items() if model.startswith(k)), 8)),
            'slots': [int(s) for s in d.lugS],
            'rel': fl(d.lug_rel[d.lugS]), 'phi': fl(d.lug_phi[d.lugS]), 'm': [int(v) for v in d.lug_m[d.lugS]],
            'theta': float(d.lug_theta), 'sigma': float(d.LUG_SIGMA),
        },
        'tube_cluster': [int(v) for v in (np.abs(np.log(d.LF[d.nl // 2] / d.f_main)) < np.log(1.10))] if d.is_octoban else None,
        'grid_step': GRID_STEP,
        'strike_gain_grid': gain_grid,
        'shape2_grid': shape_grid,
    }
    if out['kind'] == 'snare':
        k = E.Kit({key: d}, snare_buzz=0.5)
        S = k.sym[key]
        out['sym'] = {'freq': fl(S['F']), 'decay': fl(S['D']), 'w': fl(S['w']), 'href': float(S['href']),
                      'noise_peak': fl(S['nb'])}
    return out


def burst_corr():
    sos = [butter(2, [lo, min(hi, 20000)], 'band', fs=SR, output='sos') for lo, hi in E.BURST_BANDS]
    rb = np.random.default_rng(0).standard_normal(1 << 16)
    py = [1 / np.sqrt(np.mean(sosfilt(s, rb)[2000:] ** 2)) for s in sos]
    return [float(p / exact_norm(s)) for p, s in zip(py, sos)]


def same(a, b, tol=1e-9):
    """Equal up to float rounding (numpy/scipy versions may differ in the last digits)."""
    if isinstance(a, dict):
        return isinstance(b, dict) and a.keys() == b.keys() and all(same(a[k], b[k], tol) for k in a)
    if isinstance(a, list):
        return isinstance(b, list) and len(a) == len(b) and all(same(x, y, tol) for x, y in zip(a, b))
    if isinstance(a, float) or isinstance(b, float):
        return abs(a - b) <= tol * max(1.0, abs(a), abs(b))
    return a == b


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--out', default=os.path.join(ROOT, 'resources', 'bake'))
    a = ap.parse_args()
    files = {}
    files['kit.bake.json'] = {
        'sample_rate_baked': SR, 'block': E.BLK,
        'drums': [k for k, _ in KIT],
        'burst_bands': [[float(lo), float(hi)] for lo, hi in E.BURST_BANDS],
        'burst_norm_corr': burst_corr(),
    }
    for key, model in KIT:
        files[f'{key}.bake.json'] = bake_drum(key, model)
    bad = []
    os.makedirs(a.out, exist_ok=True)
    for name, obj in files.items():
        text = json.dumps(obj, separators=(',', ':')) + '\n'
        p = os.path.join(a.out, name)
        if a.check:
            if not os.path.exists(p) or not same(json.load(open(p)), json.loads(text)):
                bad.append(name)
        else:
            open(p, 'w').write(text)
            print(f'{name}: {len(text) / 1e6:.2f} MB')
    if bad:
        print('bake differs from resources/bake/:', ', '.join(bad)); sys.exit(1)


if __name__ == '__main__':
    main()
