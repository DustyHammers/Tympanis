#!/usr/bin/env python3
"""Writes the busy kit sequences used to check that the threaded kit renders bit for bit as one
thread (and as main before it): kit_busy_mono.json (tympanis_render) and kit_busy_stereo.json
(tympanis_hosttest render). Deterministic (fixed seed); rerun only to change them.

Every drum and articulation is struck many times over 8 s: groups of hits on one sample, flams
(hits a few samples apart), rolls on single drums and quiet gaps, with the snare buzz on and
uneven lug tension on some drums (twin oscillators). The stereo one also moves levels and pans
during the render, so the gains ramp."""
import json, random, os

HERE = os.path.dirname(os.path.abspath(__file__))
ROWS = [('kick1', 'head'), ('kick2', 'head')] + [(f'snare{i}', a) for i in range(1, 5) for a in ('head', 'rim', 'side')] \
    + [(f'tom{i}', a) for i in range(1, 7) for a in ('head', 'rim')] + [('gong', 'head')] + [(f'octo{i}', 'head') for i in range(1, 5)]
ROW_ID = {(d, a): d if a == 'head' else f'{d}_{a}' for d, a in ROWS}

def events(sr):
    rnd = random.Random(20261125)
    ev = []
    t = 0.05
    while t < 7.2:
        r = rnd.random()
        if r < 0.25:                      # a group on one sample
            for d, a in rnd.sample(ROWS, rnd.randint(3, 8)):
                ev.append((t, d, a, rnd.uniform(0.3, 1.0)))
        elif r < 0.45:                    # a flam: a few samples apart
            d, a = rnd.choice(ROWS)
            ev.append((t, d, a, rnd.uniform(0.4, 0.9)))
            d2, a2 = rnd.choice(ROWS)
            ev.append((t + rnd.randint(1, 40) / sr, d2, a2, rnd.uniform(0.5, 1.0)))
        elif r < 0.6:                     # a roll on one drum
            d, a = rnd.choice(ROWS)
            for k in range(rnd.randint(4, 10)):
                ev.append((t + k * 0.03, d, a, rnd.uniform(0.3, 0.8)))
        else:
            d, a = rnd.choice(ROWS)
            ev.append((t, d, a, rnd.uniform(0.2, 1.0)))
        t += rnd.choice([0.011, 0.04, 0.09, 0.13, 0.25, 0.5])
    return sorted(ev)

sr = 44100
ev = events(sr)
mono = {'sample_rate': sr, 'duration': 8.0, 'snare_buzz': 0.8,
        'drums': {'tom3': {'lug_detune': 1.5}, 'snare1': {'lug_detune': 1.0}, 'gong': {'lug_detune': 0.8}, 'kick2': {'lug_detune': 0.5}},
        'events': [[round(t, 6), d, round(v, 4), a] for t, d, a, v in ev]}
json.dump(mono, open(os.path.join(HERE, 'kit_busy_mono.json'), 'w'), indent=0)

rnd = random.Random(7)
auto = []
for k in range(40):
    tt = round(0.1 + k * 0.19, 4)
    d = rnd.choice(['kick1', 'kick2', 'snare1', 'snare2', 'tom1', 'tom3', 'tom5', 'gong', 'octo2'])
    if rnd.random() < 0.5:
        auto.append([tt, f'{d}_level', round(rnd.uniform(-18, 3), 2)])
    else:
        auto.append([tt, f'{d}_pan', round(rnd.uniform(-1, 1), 2)])
auto.append([2.0, 'snare_buzz', 0.9])
auto.append([5.0, 'main_volume', -4.0])
stereo = {'sample_rate': sr, 'block': 256, 'duration': 8.0,
          'params': {'snare_buzz': 0.6, 'tom3_lug_detune': 0.6, 'snare1_lug_detune': 0.4},
          'automation': auto,
          'events': [[round(t, 6), ROW_ID[(d, a)], int(round(20 + 107 * v))] for t, d, a, v in ev]}
json.dump(stereo, open(os.path.join(HERE, 'kit_busy_stereo.json'), 'w'), indent=0)
print(len(ev), 'hits,', len(auto), 'parameter moves')
