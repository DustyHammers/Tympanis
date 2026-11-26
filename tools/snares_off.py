#!/usr/bin/env python3
"""The Popcorn Snare's snares-off model, derived from the kit's measured wires-on / wires-off pair.

Snare 1 (SNRE_720, wires on) and Snare 2 (SNRE_722, wires off) are the same drum's body: the same
mode clusters, with the wires off ringing 3-8x longer and the 2-16 kHz wire noise 10-30 dB lower
and short (Kris, 2026-11-26; docs/ENGINE.md, "Snares on and off"). This script measures that
change and applies it to the Popcorn Snare (SNRE_725, wires on), writing

    resources/models/SNRE_725_SPsnare4_off_derived.json

Per articulation (head, rim, side) and velocity layer (matched by relative position):
  - modal decay: the ratio off / on of the energy-weighted (amp^2 / decay) mean log decay rate,
    in bins of f / f_main (the body's modes scale with the head), applied to every slot;
  - noise bands below 1 kHz (the body's unresolved ring): level and decay ratios looked up at the
    same place relative to the main mode; at and above 1 kHz (the wires, fixed in frequency): by
    absolute band. Noise T60 capped at 1 s, as in every snare model.
Amplitudes, phases, glide and the modelled transients (pulses, modes, burst) are unchanged.

Locked (Kris, 2026-11-26): docs/locked.md5 holds its md5; CI checks it is current (--check).

Check: the same transform applied to Snare 1 comes within about 5 dB (rms over octave bands and
time windows) of the real Snare 2, from about 23 dB (head); the wire bands within 2-3 dB. Kris
approved the Popcorn's sound by ear (render 46b, 2026-11-26).

Usage: python3 tools/snares_off.py --check    (fails if the committed model differs)
       python3 tools/snares_off.py            (writes the model)
The model it writes is locked (Kris, 2026-11-26; docs/locked.md5): rewrite it only when Kris asks.
The locked models it reads are only read.
"""
import argparse, copy, json, math, os, sys
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'prototype'))
import engine as E  # noqa: E402

MODELS = os.path.join(ROOT, 'resources', 'models')
ON, OFF = 'SNRE_720_SPsnare1_shared', 'SNRE_722_SPsnare2_shared'
TARGET, OUT = 'SNRE_725_SPsnare4_shared', 'SNRE_725_SPsnare4_off_derived'
EDGES = np.array([0.0, 0.8, 0.92, 1.08, 1.3, 1.7, 2.3, 3.2, 4.5, 6.5, 9.5, 14, 22, 200.0])
WIRE_HZ = 1000.0
NOISE_T60_CAP = 1.0


def load(name):
    return json.load(open(os.path.join(MODELS, name + '.json')))


def f_main(name):
    return float(E.ModalDrum(os.path.join(MODELS, name + '.json'), variation=False).f_main)


def decay_curve(lon, loff, fon, foff):
    """log(d_off / d_on) per EDGES bin; empty bins filled from their neighbours."""
    out = []
    for a, b in zip(EDGES[:-1], EDGES[1:]):
        vals = []
        for L, fm in ((lon, fon), (loff, foff)):
            F, D, A = (np.array(L[k], float) for k in ('freq', 'decay', 'amp'))
            x = F / fm
            m = (A != 0) & (D > 0) & (x >= a) & (x < b)
            if m.sum() == 0:
                vals.append(None); continue
            w = A[m] ** 2 / D[m]
            vals.append(np.sum(w * np.log(D[m])) / np.sum(w))
        out.append(None if None in vals else vals[1] - vals[0])
    xs = [i for i, v in enumerate(out) if v is not None]
    return np.interp(range(len(out)), xs, [out[i] for i in xs])


def noise_ratios(lon, loff):
    return [(math.log(max(b2['peak_rms'], 1e-9) / max(b1['peak_rms'], 1e-9)), math.log(b2['decay'] / b1['decay']))
            for b1, b2 in zip(lon['noise'], loff['noise'])]


def at(curves, t):
    ts = np.linspace(0, 1, len(curves))
    return np.array([np.interp(t, ts, [c[k] for c in curves]) for k in range(len(curves[0]))])


def derive():
    on, off, tg = load(ON), load(OFF), load(TARGET)
    fon, foff, ftg = f_main(ON), f_main(OFF), f_main(TARGET)
    res = copy.deepcopy(tg)
    for art in tg['articulations']:
        lon, loff = on['articulations'][art]['layers'], off['articulations'][art]['layers']
        n = min(len(lon), len(loff))
        pick = lambda L, i: L[int(round(i * (len(L) - 1) / (n - 1)))]
        dc = [decay_curve(pick(lon, i), pick(loff, i), fon, foff) for i in range(n)]
        nr = [noise_ratios(pick(lon, i), pick(loff, i)) for i in range(n)]
        centres = np.array([math.sqrt(b['lo'] * b['hi']) for b in lon[0]['noise']])
        layers = res['articulations'][art]['layers']
        for li, L in enumerate(layers):
            t = li / max(1, len(layers) - 1)
            c = at(dc, t)
            k = np.clip(np.searchsorted(EDGES, np.array(L['freq']) / ftg, side='right') - 1, 0, len(c) - 1)
            L['decay'] = [float(d * math.exp(c[j])) for d, j in zip(L['decay'], k)]
            lvl = at([[q[0] for q in r] for r in nr], t)
            dcy = at([[q[1] for q in r] for r in nr], t)
            for b in L['noise']:
                cen = math.sqrt(b['lo'] * b['hi'])
                if cen >= WIRE_HZ:
                    j = int(np.argmin(np.abs(np.log(centres / cen))))
                else:
                    j = int(np.argmin(np.abs(np.log(centres / fon) - math.log(cen / ftg))))
                b['peak_rms'] = float(b['peak_rms'] * math.exp(lvl[j]))
                b['decay'] = float(max(b['decay'] * math.exp(dcy[j]), 6.91 / NOISE_T60_CAP))
    res['fix'] = tg.get('fix', '') + ' + snares off, derived from the SNRE_720 / SNRE_722 wires-on / wires-off pair (tools/snares_off.py)'
    res['locked'] = ('2026-11-26 Kris: locked ("merge and lock"), the Popcorn Snare\'s wires-off model, derived by '
                     'tools/snares_off.py; sound approved by ear (render 46b)')
    res['derived_from'] = {'target': TARGET, 'on': ON, 'off': OFF, 'tool': 'tools/snares_off.py'}
    return res


def same(a, b, tol=1e-9):
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
    a = ap.parse_args()
    res = derive()
    path = os.path.join(MODELS, OUT + '.json')
    if a.check:
        if not os.path.exists(path) or not same(json.load(open(path)), json.loads(json.dumps(res))):
            print(f'{OUT}.json differs from what tools/snares_off.py makes'); sys.exit(1)
        return
    with open(path, 'w') as f:
        json.dump(res, f, separators=(',', ':'))
        f.write('\n')
    print(f'{path}: {os.path.getsize(path) / 1e6:.2f} MB')


if __name__ == '__main__':
    main()
