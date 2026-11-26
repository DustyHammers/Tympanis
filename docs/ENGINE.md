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
- **Shell depth** (`Drum::setDepth`, plugin only, not in the prototype; Kris, 2026-11-25): the models hold the recorded depth only, so a deeper or shallower shell comes from the air it encloses, q = recorded depth / depth. The modes below 1.3 x the main mode (the fundamental's group, which moves the air between the heads) get f' = f sqrt((1 + 0.12 q) / 1.12) (the air takes 12 % of their stiffness, the air spring going as 1 / depth) and decay rates x sqrt(q) (shallower: shorter); the slots within 3 % of the recorded shell's first axial air mode (c / 2 x depth) move as 1 / depth. At the recorded depth the output is bit-identical. Toms 1-4 at 4" shallower: their fundamental about 0.5 to 0.7 semitone up.
- **Strike position** gain per slot (heads; not kicks or side sticks), energy kept constant.
- **Uneven lug tension** (`lug_detune`, 0 .. 2): split slots get a twin oscillator a little below; the strike angle shares the hit between the pair. 0 = evenly tuned; 1 = the prototype's 1 (5 % RMS tension error); the plugin's Lug Detune reaches 2 (10 %), past the prototype's range (Kris, 2026-11-25), so the port check covers 0 .. 1.
- **Tension-modulation glide**, two stages, accumulating in rolls (capped).
- **Noise layer**: 19 third-octave bands with exponential decay.
- **Modelled contact transient**: damped modes, Ricker pulses, a noise burst in 21 third-octave bands from 900 Hz up.

## Kit

`Kit` renders the 17 engine drums (`resources/bake/kit.bake.json`; `plugin/KitMap.h` `kEngineDrums`): the plugin's 15 drums, with the Snare and the Popcorn Snare as two models each, wires on and wires off ("Snares on and off" below). It renders them (mono at unity, or stereo with per-drum gains for the plugin's level and pan) and the **snare buzz** (kit parameter, 0..1): the rest of the kit drives each snare's head modes below 1.5 kHz; when the head motion passes the wires' seating level, the snare's own wire noise rattles.

**Parallel drums, serial kit** (Kris, 2026-11-25; a step of "Cymbalis + Tympanis: one physically modelled kit"). `Kit::renderChunk` works in four stages:

1. **Every drum into its own buffer.** With a pool (`Kit::setPool`, borrowed: the caller owns and prepares it, off the audio path), the drums that are sounding go through `dhpool::JobPool::run` (`third_party/dhpool`, a copy of Cymbalis's `shared/dhpool`), heaviest first. The cost is `Drum::ringingSlots()`, its active modal slots; ties keep drum order. The silent drums (`Drum::silent()`) are rendered on the calling thread after the run: they cost little, and so every drum's state moves exactly as before. With fewer than two drums sounding, or no pool, everything is on the calling thread, as it was.
2. **The mix**, summed in drum order on the calling thread.
3. **The snare buzz**, snares in order: each snare is driven by the mix with the earlier snares' sympathetic ring and wires already in it.
4. **The per-drum gain ramps** into L and R, in drum order, after the snare pass has added into the snares' own buffers.

Why 2-4 stay serial: float addition is not associative, so the sum has to be in a fixed order to give the same bits at any thread count; the snare pass is serial by nature (each snare hears the earlier snares' buzz); the gain stage needs the snares' buffers after that pass. They are cheap next to stage 1.

**The same bits at any thread count** (2026-11-25), and as before that change: `tympanis_hosttest checks` compares 2 and 3 threads with 1 over a busy sequence (buzz up, uneven lug tension, levels and pans moving); CI renders `tools/seq/kit_busy_mono.json` at 1, 2 and 3 threads and requires one md5. On 2026-11-25 (Linux, GCC 13.3, Release) `tools/seq/kit_busy_mono.json` gave md5 `b5dcf03d5c187d6c4f682501665c6be0` (`tympanis_render --raw`) and `tools/seq/kit_busy_stereo.json` `0f4cc91ba982eac9a6a6c9bc044933b9` (`tympanis_hosttest render --raw`) on `main` before the change and at 1, 2, 3 (and 4) threads after it. ThreadSanitizer (GCC 13, Linux) on the busy render at 2, 3 and 4 threads and on the pool's self-test: no data race (CI runs it at 4, with full tests). The busy sequences were remade on 2026-11-26 for the Snare and Popcorn Snare switches (the mono one strikes all 17 engine drums; the stereo one flips both switches): `kit_busy_mono.json` md5 `276a98639bb4291dea3e2cac2ae61da7`, `kit_busy_stereo.json` `3d7b644e87b648d5750a2f9348ad4438`, the same at 1, 2 and 3 threads.

**Shared state.** Nothing is shared between drums while they render: no static or global state in the engine (`ringBlock` is a pure function); each drum has its own variation generator (seeded from its model's name), noise banks, transient, pulse and burst pools and glide state; the baked kit data is read-only; each snare's wire noise belongs to the serial snare pass. The pool hands each job the caller's floating-point control (flush to zero). The analysis is in Cymbalis's `docs/PORT_MAP.md`, "Multi-cymbal rendering (step 2)", "Driving Tympanis's `Kit` with dhpool"; this change follows it.

**Runs.** The processor renders each stretch between note-ons separately (and `renderStereo` splits a stretch into pieces of at most 1024 samples): each piece with two or more drums sounding is one pool run, so a block with hits inside it costs more than one. The bench reports runs per block. A size below which the caller renders alone is a later step.

**The plugin renders on one thread.** `TympanisProcessor` owns a pool but uses it only when `setRenderThreadsForTesting` (`tympanis_hosttest` only; not a parameter, not saved, not in the editor) asks for more than one thread. Turning threads on is a later step, with Cymbalis's, after tests in hosts: the pool's wake-up made 2 threads slower than 1 at 96 kHz / 64 samples in Cymbalis's measurements.

## Bake

`tools/bake_kit.py` turns the locked models into `resources/bake/*.bake.json`. It copies the model data and adds what the C++ engine cannot compute itself (no Bessel functions, no numpy random generator):

- strike-position gain per slot on a radius grid (0 .. 0.95, step 0.01), per articulation (head and rim; each uses its own amplitudes, as the prototype)
- squared mode shape per slot on the same grid (re-strike damping)
- the lug tables (twin slots, split size, eigen-angles, strike angle)
- per-band level corrections: the prototype measures each band filter's noise gain on a fixed random sequence; the ratio to the exact gain is kept so the levels match at any sample rate
- the snare-head bank for the snare buzz

The JSON bake stays the source. The plugin embeds it packed (docs/BUILDS.md, "The packed kit"): every number keeps its exact bits, so the plugin loads the same values as the JSON gives and sounds the same. `python3 tools/bake_kit.py --check` fails if the committed bake differs from what the tool makes. Its `KIT` list is the engine's drum order (and the snare-buzz order); `RESERVED` drums are baked and checked but not in the kit or the plugin (Snare 3, since 2026-11-26); `NO_SYM` snares get no sympathetic bank (the Popcorn Snare's wires-off model). `python3 tools/snares_off.py --check` fails if the derived wires-off model differs from what that tool makes.

## Port notes (deliberate differences from the prototype)

- Noise uses different random sequences (same spectra and levels).
- Hit-to-hit variation uses its own generator, seeded per drum.
- Strike-position gain and mode shapes come from the baked grids, interpolated between grid points (exact at them; the native radius 0.35 and the contact radii are grid points).
- Snare 4's (the Popcorn Snare's, and its wires-off model's) reference size is 10" (the bake's `native_in`, `CATALOG_IN` in `tools/bake_kit.py`; Kris, 2026-11-24), where the prototype's NATIVE_IN has 14" for every snare. The model is unchanged: at its catalog size it sounds as before; a Size of S" in the plugin is the prototype's size_in = 1.4 x S.
- A size change keeps the noise bands' baked level corrections (the prototype re-measures the moved bands on its random sequence). The band filters are re-designed live, with the exact noise gain from the filters' state-space Gramian (cheap, allocation-free).
- A slot that has decayed 140 dB below a full hit is cleared and skipped; glide below 1e-12 counts as ended and the poles stop being recomputed (CPU).
- With `lug_detune` turned to 0 while twins still ring, the twins ring on at even tension (the prototype froze them and resumed them later).
- Transients, pulses and bursts live in fixed pools (24 / 256 / 8 per drum); when full, the oldest is replaced.
- Real time: the plugin renders in pieces of at most 1024 samples, so a transient ends 0.25 s after its hit (the prototype ended it at the first event after 0.25 s).

## Checks

- `python3 tools/ref_check.py --render build/tools/tympanis_render/tympanis_render`: 264 hits over every drum and articulation, rolls, an overlapping groove; lug detune, tune, decay, strike position (head and rim) and size set on several drums; snare buzz on; variation off and noise silenced on both sides. Max |C++ - Python| = 4e-7 of the peak (-128 dB; the silence gate above; -203 dB without it).
- `--noise`: noise on, third-octave band energies over the whole sequence: within 0.4 dB.
- `tympanis_hosttest checks`: the processor (see `docs/PLUGIN.md`), and the drums through a pool bit-identical to one thread ("Kit").
- `dhpool_selftest`: the pool's own self-test (Mac and Linux CI).

## Snares on and off

(Kris, 2026-11-26.) The Snare and the Popcorn Snare each have two models, wires on and wires off, behind their Snares On/Off switch (`docs/PLUGIN.md`). The switch picks the engine drum a new hit goes to; the ringing one rings on. The processor sends every other setting to both models, at the same moment.

**The Snare** is Snare 1's model (wires on) and Snare 2's (wires off), unchanged. The two are the same body: the same mode clusters (about 200-225 Hz and near 505 Hz), the wires-off modes ringing 3-8x longer, the 2-16 kHz wire noise 10-30 dB lower and short; Snare 2 sits about 2/3 semitone lower (two tunings, kept: Kris wants Snare 2 exactly as it was).

**Snare 2 plays the same bits as before** (proved 2026-11-26, raw floats through `processBlock`, `main` a8a8496 against this change, at 64- and 256-sample blocks, all eight equal): Snare 2 alone with the buzz off; alone with the buzz on (Snare 1 rings along); a busy kit sequence with its hits and the buzz on; the same with Snare 1's hits. On `main` Snare 3 and Snare 4 were muted in the buzz cases (Snare 3 is gone, and the Popcorn Snare's ring along changed: below). The Popcorn Snare's wires-on model alone also plays Snare 4's bits. It holds because Snare 1 and 2 keep their engine places (2 and 3) and every setting reaches them as before.

**The Popcorn Snare's wires-off model** is derived (`tools/snares_off.py`, written to `resources/models/SNRE_725_SPsnare4_off_derived.json`, `--check` in a pull request that changes it): the Snare's on/off change, measured per articulation and velocity layer (the ratio of the energy-weighted mean decay rate in bins of f / f_main; the noise bands' level and decay ratios, below 1 kHz at the same place relative to the main mode, above by absolute band), applied to Snare 4's model; amplitudes, phases, glide and transients unchanged. As a check, the same transform takes Snare 1 to within about 5 dB of the real Snare 2 (rms over octave bands and time windows; 23 dB before), the wire bands within 2-3 dB; what is left is the two takes' tuning and low-end balance. Kris approved the Popcorn's sound by ear (render 46b).

**The buzz.** Snare Buzz works as before. Both Snare models ring along when other drums play (as Snare 1 and 2 did). The Popcorn Snare's wires-off model has no sympathetic bank (`tools/bake_kit.py` NO_SYM): it plays only when hit, so nothing new joins the kit's ring along. What changed: Snare 3's ring along is gone (Kris, 2026-11-26), and the Popcorn Snare's sympathetic wire noise draws a different random sequence (it is seeded by the engine drum's place, which moved from 5 to 4; the same sound, another draw).

## CPU

`tympanis_hosttest bench [--seconds S] [--one-thread]`. The hits: every 250 ms, alternate halves of the 25 rows are struck (row r when r + k is even, k the step; velocities 64-127), so all 15 drums keep ringing (the snares' wires-on models: the switches at their default; a snare switched while it rings has both models sounding for a while, one drum more); snare buzz on. Two tables (`--one-thread`: the first only, as CI runs it unless full tests are asked for):

1. **One thread, back to back** (unchanged, the plugin's real-time figure): 20 s at 44.1, 48 and 96 kHz, 64- and 256-sample blocks. On a 2.1 GHz Xeon (Linux, one core): about 6x real time at 44.1 kHz, 5.5x at 48 kHz and 2.7x at 96 kHz (2026-11-26, 15 drums; about 4.7x, 4.2x and 2.2x with the 17 before).
2. **Per block, paced** (2026-11-25): after a 1 s lead, each block starts at its deadline on a real-time thread, as in a host, and is timed (S s per case, 4 in CI), at 1, 2 and 3 threads (the drums through the pool). Per case: the block time as a % of the block (mean, p99, worst, blocks over 100 %); pool runs per block; the caller's wait in shared runs (median / p99 / max); the workers' wake-up delay (median / p99, the share that found them asleep); the workers' CPU (% of one core, spinning included). Then the bench's own time.

The CI job prints both for the macOS runner (Apple M1, virtual) in its summary. First CI figures (run 45681418531, 2026-11-25, 4 s per paced case; the bench step took 115 s, the pool's self-test 15 s):

- One thread, back to back: 5.4-5.5x real time at 44.1 kHz, 4.0-4.1x at 48 kHz, 2.1-2.7x at 96 kHz.
- Paced, block time as % of the block, mean / worst / blocks over 100 %:

| Rate / block | 1 thread | 2 threads | 3 threads |
|---|---|---|---|
| 44.1 kHz / 64 | 24 / 236 / 20 | 14 / 126 / 2 | 11 / 190 / 4 |
| 44.1 kHz / 256 | 20 / 126 / 1 | 10 / 45 / 0 | 7.0 / 54 / 0 |
| 48 kHz / 64 | 28 / 334 / 20 | 23 / 1307 / 60 | 16 / 433 / 30 |
| 48 kHz / 256 | 26 / 97 / 0 | 15 / 104 / 1 | 11 / 138 / 1 |
| 96 kHz / 64 | 55 / 521 / 853 | 28 / 619 / 46 | 23 / 559 / 20 |
| 96 kHz / 256 | 48 / 229 / 184 | 26 / 357 / 10 | 20 / 169 / 3 |

- Runs per block 1.00-1.02. Wake-up median 13-24 us (p99 47-645 us), nearly always from sleep. Caller wait median 0.1-8 us, p99 24-561 us, max 0.8-3.9 ms (a worker stopped mid-job on this virtual machine, as in Cymbalis's runs). Workers 8.5-41 % of a core.
- Reading it as Cymbalis's runs are read: one 4 s run per case on a virtual machine, so differences under about 1.5x are noise (48 kHz / 64 at 2 threads, worse than 1, is such a run). The large ones hold: the mean block time halves at 2-3 threads, and at 96 kHz the blocks over the deadline go from hundreds to tens or a few.

Locally (Linux VM, 2.1 GHz Xeon, 4 cores, 2 s per case), 3 threads against 1: mean block time 26 -> 21 % at 44.1 kHz / 64, 28 -> 14 % at 44.1 kHz / 256, 56 -> 37 % at 96 kHz / 64 (blocks over 100 %: 46 -> 16); runs per block 1.00-1.02; wake-ups 37-90 us median at 2 threads (150-300 us at 3, this VM); the workers 10-37 % of a core. A wake-up is a large part of a 64-sample block, which is why turning threads on waits for tests in hosts.

The bank, noise bands, bursts and transients run SIMD across modes/bands (`#pragma omp simd`, SSE4.1 on x86_64).

## Model note

Tom 6 has one slot at 134.8 Hz with almost no decay (0.028 /s, T60 about 4 minutes) at a low level: the drum is still at -79 dB of its peak 29 s after a hit. Inaudible, but it keeps that slot running; the model is locked, so it is left as is.
