# Engine

The C++ engine (`plugin/dsp/`, no JUCE) is a port of `prototype/engine.py` (md5 `735fd0ea`), the Python engine every approved render came from.

## Per drum

One complex resonator bank per drum (`Drum`). Re-strikes add into the ringing mode states, so each drum is polyphonic for free. All articulations of a drum (head, rim shot, side stick) play the same bank: the models share their slots.

On each hit (`Drum::strike`):

- **Re-strike damping** (hardwired): the stick or beater loads what is already ringing, per mode by its shape at the contact point and its period against the contact time; the loaded part is removed over the contact time. The previous hits' contact modes and the wire noise are damped too.
- **Hit-to-hit variation** (hardwired): velocity N(0, 0.02), spectral tilt N(0, 0.4) dB/oct above 2x the main mode, strike position N(0, 0.015) on heads (never kicks), lug strike angle N(0, 0.15).
- **Contact noise burst (fixed 2026-11-24)**: `NoiseBank::processEnv` reads its levels sample-major, `env[i * stride + j]`, as `renderBurstsPart` writes them (`[sample][band]`). Until then it read `env[j * stride + i]`: each band's envelope came from the wrong sample and band, switching it on and off sample by sample, and the bursts crackled (heard on Octoban 2 and the gong drum at medium to high velocities; every drum had it). The whole-render noise check averages this away; `tools/ref_check.py --burst` compares the noise alone (with noise minus without) in 2-40 ms windows after single hits on every drum, and its kurtosis, with the prototype (CI runs it).
- **Level trims**: `Drum::setArtTrim` scales everything one articulation's strikes make (modes, noise, contact transient); the plugin uses it for four hot tom rim shots (`docs/PLUGIN.md`, Output). Default 1: the port checks are unaffected.
- **Velocity layers**: log-amplitude / linear frequency and decay interpolation between the two nearest layers; phase from the nearer layer. Level span 24 dB (the setting of every approved render).
- **Strike position** gain per slot (heads; not kicks or side sticks), energy kept constant.
- **Uneven lug tension** (`lug_detune`): split slots get a twin oscillator a little below; the strike angle shares the hit between the pair. 0 = evenly tuned.
- **Tension-modulation glide**, two stages, accumulating in rolls (capped).
- **Noise layer**: 19 third-octave bands with exponential decay.
- **Modelled contact transient**: damped modes, Ricker pulses, a noise burst in 21 third-octave bands from 900 Hz up.

## Kit

`Kit` renders the 17 drums (mono at unity, or stereo with per-drum gains for the plugin's level and pan) and the **snare buzz** (kit parameter, 0..1): the rest of the kit drives each snare's head modes below 1.5 kHz; when the head motion passes the wires' seating level, the snare's own wire noise rattles.

## Bake

`tools/bake_kit.py` turns the locked models into `resources/bake/*.bake.json`. It copies the model data and adds what the C++ engine cannot compute itself (no Bessel functions, no numpy random generator):

- strike-position gain per slot on a radius grid (0 .. 0.95, step 0.01), per articulation (head and rim; each uses its own amplitudes, as the prototype)
- squared mode shape per slot on the same grid (re-strike damping)
- the lug tables (twin slots, split size, eigen-angles, strike angle)
- per-band level corrections: the prototype measures each band filter's noise gain on a fixed random sequence; the ratio to the exact gain is kept so the levels match at any sample rate
- the snare-head bank for the snare buzz

The CMake build gzips the bake into the plugin. `python3 tools/bake_kit.py --check` fails if the committed bake differs from what the tool makes.

## Port notes (deliberate differences from the prototype)

- Noise uses different random sequences (same spectra and levels).
- Hit-to-hit variation uses its own generator, seeded per drum.
- Strike-position gain and mode shapes come from the baked grids, interpolated between grid points (exact at them; the native radius 0.35 and the contact radii are grid points).
- Snare 4's reference size is 10" (the bake's `native_in`, `CATALOG_IN` in `tools/bake_kit.py`; Kris, 2026-11-24), where the prototype's NATIVE_IN has 14" for every snare. The model is unchanged: at its catalog size it sounds as before; a Size of S" in the plugin is the prototype's size_in = 1.4 x S.
- A size change keeps the noise bands' baked level corrections (the prototype re-measures the moved bands on its random sequence). The band filters are re-designed live, with the exact noise gain from the filters' state-space Gramian (cheap, allocation-free).
- A slot that has decayed 140 dB below a full hit is cleared and skipped; glide below 1e-12 counts as ended and the poles stop being recomputed (CPU).
- With `lug_detune` turned to 0 while twins still ring, the twins ring on at even tension (the prototype froze them and resumed them later).
- Transients, pulses and bursts live in fixed pools (24 / 256 / 8 per drum); when full, the oldest is replaced.
- Real time: the plugin renders in pieces of at most 1024 samples, so a transient ends 0.25 s after its hit (the prototype ended it at the first event after 0.25 s).

## Checks

- `python3 tools/ref_check.py --render build/tools/tympanis_render/tympanis_render`: 264 hits over every drum and articulation, rolls, an overlapping groove; lug detune, tune, decay, strike position (head and rim) and size set on several drums; snare buzz on; variation off and noise silenced on both sides. Max |C++ - Python| = 4e-7 of the peak (-128 dB; the silence gate above; -203 dB without it).
- `--noise`: noise on, third-octave band energies over the whole sequence: within 0.4 dB.
- `tympanis_hosttest checks`: the processor (see `docs/PLUGIN.md`).

## CPU

`tympanis_hosttest bench`: every drum and articulation struck every 250 ms (about 60 hits per second, all 17 drums ringing), snare buzz on. On a 2.1 GHz Xeon (Linux, one core): about 4x real time at 44.1 and 48 kHz, 1.9x at 96 kHz. The CI job prints the figure for the macOS runner. The bank, noise bands, bursts and transients run SIMD across modes/bands (`#pragma omp simd`, SSE4.1 on x86_64).

## Model note

Tom 6 has one slot at 134.8 Hz with almost no decay (0.028 /s, T60 about 4 minutes) at a low level: the drum is still at -79 dB of its peak 29 s after a hit. Inaudible, but it keeps that slot running; the model is locked, so it is left as is.
