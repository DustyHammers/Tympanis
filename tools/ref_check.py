#!/usr/bin/env python3
"""Port check: the C++ engine against the Python prototype (prototype/engine.py), sample by sample.

Both engines run the same sequence with hit-to-hit variation off and every noise source
silenced (the two use different random sequences, so noise can only be compared by its
spectrum: see --noise). What is left is deterministic: the resonator banks, glide, re-strike
damping, lug twins, strike position, the modelled transients and pulses, and the snare-head
coupling of the snare buzz. Prints the error relative to the signal's peak.

    python3 tools/ref_check.py --render build/tools/tympanis_render/tympanis_render
    python3 tools/ref_check.py --render ... --noise     (noise on, compares band energies)
"""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'prototype'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import engine as E  # noqa: E402
from bake_kit import KIT, NO_SYM  # noqa: E402

SR = E.SR


class Silent:
    def standard_normal(self, n): return np.zeros(n)


def sequence():
    """Every drum and articulation, single hits at several velocities, and rolls (re-strikes)."""
    ev = []; t = 0.05
    for key, model in KIT:
        arts = ['head']
        m = json.load(open(os.path.join(ROOT, 'resources', 'models', model + '.json')))
        if 'articulations' in m: arts = list(m['articulations'])
        for art in arts:
            for v in (1.0, 0.55):
                ev.append([round(t, 4), key, v, art]); t += 0.35
            for k in range(6):                       # a roll: re-strike damping and glide build-up
                ev.append([round(t + k * 0.045, 4), key, 0.5 + 0.08 * k, art])
            t += 0.6
    # a groove where drums overlap (snare buzz driven by the others)
    for k in range(16):
        ev.append([round(t + k * 0.15, 4), 'kick1' if k % 4 == 0 else 'tom3' if k % 4 == 2 else 'snare2', 0.85, 'head'])
    t += 16 * 0.15 + 1.0
    return ev, t


DRUM_PARAMS = {
    'tom2': {'lug_detune': 1.0}, 'snare4_off': {'lug_detune': 0.6, 'tune_semi': -1.0}, 'kick2': {'tune_semi': 1.5},
    'octo3': {'lug_detune': 0.8}, 'gong': {'lug_detune': 0.5, 'tune_semi': 0.7},
    'tom4': {'decay': 1.8}, 'tom5': {'strike_pos': 0.7}, 'snare4': {'decay': 0.6, 'strike_pos': 0.15},
    'octo1': {'size_in': 8.0}, 'tom1': {'size_in': 9.0, 'decay': 2.5},
}


def python_render(ev, dur, noise):
    drums = {}
    for key, model in KIT:
        p = dict(vel_range_db=24, variation=False)
        p.update({k: v for k, v in DRUM_PARAMS.get(key, {}).items()})
        d = E.ModalDrum(os.path.join(ROOT, 'resources', 'models', model + '.json'), **p)
        if not noise: d.rng = Silent(); d.brng = Silent()
        drums[key] = d
    kit = E.Kit(drums, snare_buzz=0.5)
    for k in NO_SYM:                 # as baked: no sympathetic bank (tools/bake_kit.py NO_SYM)
        kit.snares.remove(k); kit.sym.pop(k)
    if not noise:
        for S in kit.sym.values(): S['rng'] = Silent()
    return kit.sequence([tuple(e) for e in ev], dur)


def cpp_render(exe, ev, dur, noise):
    drums = {k: dict(vel_range_db=24, **DRUM_PARAMS.get(k, {})) for k, _ in KIT}
    seq = {'sample_rate': SR, 'duration': dur, 'variation': False, 'noiseless': not noise, 'snare_buzz': 0.5,
           'drums': drums, 'events': ev}
    with tempfile.TemporaryDirectory() as td:
        sp = os.path.join(td, 'seq.json'); rp = os.path.join(td, 'out.f64')
        json.dump(seq, open(sp, 'w'))
        subprocess.run([exe, '--bake', os.path.join(ROOT, 'resources', 'bake'), '--seq', sp, '--raw', rp, '--one-call'], check=True)
        return np.fromfile(rp, dtype=np.float64)


def band_db(x):
    from scipy.signal import butter, sosfilt
    out = []
    for k in range(27):
        lo = 50 * 2 ** (k / 3); hi = lo * 2 ** (1 / 3)
        if hi > 18000: break
        y = sosfilt(butter(4, [lo, hi], 'band', fs=SR, output='sos'), x)
        out.append(10 * np.log10(np.mean(y * y) + 1e-30))
    return np.array(out)


def burst_sequence():
    """Single hits on every drum and articulation, far apart: the contact transient's noise burst."""
    ev = []; t = 0.05
    for key, model in KIT:
        arts = ['head']
        m = json.load(open(os.path.join(ROOT, 'resources', 'models', model + '.json')))
        if 'articulations' in m: arts = list(m['articulations'])
        for art in arts:
            for v in (1.0, 0.8, 0.6):
                ev.append([round(t, 4), key, v, art]); t += 0.4
    return ev, t + 0.4


def burst_windows(x, ev):
    """x: the noise alone (a render with noise minus the same without). Its energy in 1/3-octave
    groups (0.5-2, 2-6, 6-16 kHz) in 0-2, 2-5, 5-10, 10-20 and 20-40 ms after each hit, summed over
    the hits (dB), and the kurtosis of the 2-16 kHz part in 0-40 ms (3 for gaussian noise; a
    gated or scrambled envelope raises it)."""
    from scipy.signal import butter, sosfilt
    bands = [(500, 2000), (2000, 6000), (6000, 16000)]
    wins = [(0.0, 0.002), (0.002, 0.005), (0.005, 0.010), (0.010, 0.020), (0.020, 0.040)]
    ys = [sosfilt(butter(4, b, 'band', fs=SR, output='sos'), x) for b in bands]
    hf = sosfilt(butter(4, [2000, 16000], 'band', fs=SR, output='sos'), x)
    e = np.zeros((len(bands), len(wins)))
    segs = []
    for t, *_ in ev:
        a = int(round(t * SR))
        for bi, y in enumerate(ys):
            for wi, (w0, w1) in enumerate(wins):
                seg = y[a + int(w0 * SR):a + int(w1 * SR)]
                e[bi, wi] += np.sum(seg * seg)
        s = hf[a:a + int(0.040 * SR)]
        segs.append(s / (np.sqrt(np.mean(s * s)) + 1e-30))
    z = np.concatenate(segs)
    return 10 * np.log10(e + 1e-30), float(np.mean(z ** 4))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--render', required=True)
    ap.add_argument('--noise', action='store_true')
    ap.add_argument('--burst', action='store_true', help='compare the noise (contact bursts) over time after single hits')
    ap.add_argument('--cache', help='keep the Python render in this .npy file and reuse it')
    a = ap.parse_args()
    if a.burst:
        # the noise alone: the same hits with noise minus without (the modes cancel exactly)
        ev, dur = burst_sequence()
        print(f'{len(ev)} single hits, {dur:.1f} s')
        if a.cache and os.path.exists(a.cache):
            z = np.load(a.cache); yp, yp0 = z['yp'], z['yp0']
        else:
            yp = python_render(ev, dur, True); yp0 = python_render(ev, dur, False)
            if a.cache: np.savez(a.cache, yp=yp, yp0=yp0)
        yc = cpp_render(a.render, ev, dur, True); yc0 = cpp_render(a.render, ev, dur, False)
        n = min(len(yp), len(yp0), len(yc), len(yc0))
        ep, kp = burst_windows(yp[:n] - yp0[:n], ev)
        ec, kc = burst_windows(yc[:n] - yc0[:n], ev)
        d = ec - ep
        print('noise energy after the hits, C++ - Python (dB); rows 0.5-2 / 2-6 / 6-16 kHz,')
        print('columns 0-2 / 2-5 / 5-10 / 10-20 / 20-40 ms:')
        print(np.round(d, 2))
        print(f'kurtosis of the 2-16 kHz noise in 0-40 ms: Python {kp:.2f}, C++ {kc:.2f}')
        # the first 2 ms hold little noise energy and vary from run to run: not compared
        print(f'max |difference| from 2 ms: {np.max(np.abs(d[:, 1:])):.2f} dB (limit 1.5); kurtosis difference {abs(kc - kp):.2f} (limit 0.4)')
        ok = np.max(np.abs(d[:, 1:])) < 1.5 and abs(kc - kp) < 0.4
        sys.exit(0 if ok else 1)
    ev, dur = sequence()
    print(f'{len(ev)} hits, {dur:.1f} s')
    if a.cache and os.path.exists(a.cache): yp = np.load(a.cache)
    else:
        yp = python_render(ev, dur, a.noise)
        if a.cache: np.save(a.cache, yp)
    yc = cpp_render(a.render, ev, dur, a.noise)
    n = min(len(yp), len(yc)); yp, yc = yp[:n], yc[:n]
    pk = np.max(np.abs(yp))
    if not a.noise:
        err = np.max(np.abs(yp - yc)) / pk
        print(f'peak {pk:.4f}; max |C++ - Python| / peak = {err:.2e} ({20 * np.log10(err + 1e-300):.1f} dB)')
        # per-hit-window report of the worst spot
        i = int(np.argmax(np.abs(yp - yc))); print(f'worst at {i / SR:.3f} s')
        sys.exit(0 if err < 1e-6 else 1)
    else:
        bp, bc = band_db(yp), band_db(yc)
        d = bc - bp
        print('third-octave band energy, C++ - Python (dB):', np.round(d, 2))
        print(f'max |difference| {np.max(np.abs(d)):.2f} dB')
        sys.exit(0 if np.max(np.abs(d)) < 1.0 else 1)


if __name__ == '__main__':
    main()
