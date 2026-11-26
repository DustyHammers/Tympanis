# Plugin

macOS VST3 + AU instrument (JUCE 7.0.12), universal arm64 + x86_64, macOS 11+. Identity: company DHammers, manufacturer code `Dham`, plugin code `Tymp`, bundle ID `com.dhammers.tympanis` (code and name are working choices).

## The drums (Kris, 2026-11-26)

Fifteen drums: Kick 1 and 2, the **Snare**, the **Popcorn Snare**, Toms 1-6, the Gong Drum and Octobans 1-4.

- **The Snare** has two models behind its **Snares On/Off** switch: wires on is Snare 1's model, wires off Snare 2's, each exactly as before (the same bits; `docs/ENGINE.md`, "Snares on and off"). Its parameter IDs are Snare 1's (`snare1_*`), so a saved session keeps its settings; Snare 2's parameters and rows are gone and its settings are not carried over.
- **The Popcorn Snare** (10", formerly Snare 4; IDs `snare4_*`) has the same switch: wires on is Snare 4's model, wires off a model derived from the Snare's on/off pair (`tools/snares_off.py`; Kris approved its sound, render 46b).
- The switch picks the model a new hit plays; a hit that is ringing keeps ringing as it is. Every other setting (tune, lug detune, decay, strike position, size, level, pan, velocity sensitivity) is shared by the two models.
- **Snare 3** is out of the plugin: no parameters, rows or drawing. Its model stays in `resources/models/`, and its bake stays current (`tools/bake_kit.py` RESERVED), so it can come back.

## Host parameters (122)

Per drum (`<drum>` = `kick1 kick2 snare1 snare4 tom1..tom6 gong octo1..octo4`; `snare1` is the Snare, `snare4` the Popcorn Snare):

| ID | Name | Range | Default |
|---|---|---|---|
| `<drum>_tune` | `<Drum> Tune` | -2 .. +2 semitones | 0 |
| `<drum>_lug_detune` | `<Drum> Lug Detune` | 0 .. 1 (1 = 10 % RMS tension error between lugs; Kris, 2026-11-25, was 5 %) | 0 |
| `<drum>_decay` | `<Drum> Decay` | 0.25 .. 4 x ring time (highs change less) | 1 |
| `<drum>_strike_pos` | `<Drum> Strike Position` | 0 (centre) .. 0.95 (near the rim); not on the bass drums | 0.35 |
| `<drum>_size` | `<Drum> Size` | the shell's depth, inches: -4 .. +2 in 1" steps; plays on Toms 1-4 only (greyed out on the others) | the depth as drawn |
| `<drum>_level` | `<Drum> Level` | -inf (bottom, -60) .. +6 dB | 0 dB |
| `<drum>_pan` | `<Drum> Pan` | -1 (left) .. +1 (right), -3 dB compensated constant power (0 dB centre, +3 dB hard) | 0 |
| `<drum>_snares` | `<Drum> Snares` | On / Off: which model a new hit plays, wires on or off; the Snare and the Popcorn Snare only | On |
| `<drum>_vel_sens` | `<Drum> Velocity Sensitivity` | 0 .. 1, shown 0% .. 100%. The velocity the drum plays is vf + s (v - vf), v the note's velocity / 127, vf = 100/127 (as Cymbalis). 100%: the model's full velocity response; lower narrows the dynamics toward velocity 100; 0%: every hit plays as velocity 100 | 1 (100%) |

Kit: `snare_buzz` Snare Buzz 0 .. 1 (0.5); `main_volume` Main Volume -inf .. +6 dB (0 dB), on the summed stereo output.

Decisions (Kris, 2026-11-24): decay, strike position, size, tune and lug detune exposed per drum; independent level and pan per drum; main volume on the 2-channel sum; individual outputs later. Velocity Sensitivity per drum (Kris, 2026-11-24), as Cymbalis's. The kicks have no strike position: their beater position is fixed (as sampled) and the engine ignores it. Re-strike damping and hit-to-hit variation are hardwired; glide amount, click and noise mix, velocity range (24 dB) and tube length are not exposed.

Strike position applies to head hits and rim shots (the stick's head contact), not side sticks. Tune, decay and size (depth) act live on ringing drums; strike position from the next hit. Level and pan ramp over each rendered piece (no zipper noise). A snare's sympathetic head ring and wire buzz follow that snare's level and pan; the coupling itself uses the unfaded kit, as in a room.

Catalog sizes: kicks 22", the Snare 14", the Popcorn Snare 10" (Kris, 2026-11-24), toms 10/12/13/14", floor toms 16 x 16" and 18 x 16", gong drum 22", octobans 6". Every head plays at its catalog size: Size no longer scales the head (Kris, 2026-11-25).

Size is the shell's depth (Kris, 2026-11-25), on Toms 1-4: 10, 9, 10 and 11" by default (as drawn; Tom 1 is 10" x 10", Kris, 2026-11-25), 4" shallower to 2" deeper in 1" steps (`kDepthDownIn`, `kDepthUpIn` in `KitMap.h`). At the default depth the toms sound as modelled. The other drums keep their Size parameter (their drawn depth, same range), greyed out in the editor; it changes nothing. The sound of depth is in `docs/ENGINE.md`; the drawings follow it (`docs/UI.md`).

## MIDI map (session state, not parameters)

As in Cymbalis: one MIDI channel for the plugin (default 10) and one note per row, saved with the session. Every articulation is its own row; rows of one drum play its one shared bank. Note-offs are ignored (drums ring on). A note belongs to at most one row: giving a row a note another row has sets that row to none; a saved state where rows share a note keeps it on the first row.

| Row | Default note | Row | Default note |
|---|---|---|---|
| Kick 1 | 36 | Tom 1 / rim shot | 50 / 72 |
| Kick 2 | 35 | Tom 2 / rim shot | 48 / 73 |
| Snare / rim shot / side stick | 38 / 40 / 37 | Tom 3 / rim shot | 47 / 74 |
| Popcorn Snare / rim shot / side stick | 30 / 31 / 32 | Tom 4 / rim shot | 45 / 75 |
| Gong Drum | 64 | Tom 5 / rim shot | 43 / 76 |
| Octoban 1..4 | 60..63 | Tom 6 / rim shot | 41 / 77 |

The Snare's and the Popcorn Snare's rows play whichever model their Snares switch picks. Notes 24..29 (Snare 2's and Snare 3's rows) play nothing now; a saved session's notes for those rows are ignored.

The default notes follow **General MIDI** (Kris, 2026-11-24, for now): GM's notes where GM has the sound (kicks 36/35, snare 38, side stick 37, toms 50/48/47/45/43/41; the Snare's rim shot on GM's electric snare, 40, as most GM-compatible drum maps). The rows GM has no sound for sit below GM's range (24..32) and on GM's bongo/conga notes (60..64: gong drum, octobans) and whistle/guiro/claves/wood-block notes (72..77: tom rim shots), away from its cymbals; a GM drum part that uses those percussion notes plays these drums. The editor sets the channel (MIDI CHANNEL box) and each row's note (the DRUM page's MIDI NOTE boxes: drag, pick from the list, NONE, RESET DEFAULT or MIDI LEARN: the next note-on on the channel goes to the row and plays it).

## Editor

Three pages in Cymbalis's look (docs/UI.md): DRUM, KIT and TUNING. The page, the selected drum and the editor's size are saved with the session (not parameters).

## Output

Stereo out (mono out: (L + R) / 2). Under Main Volume a fixed gain of 0.4 (Kris, 2026-11-24: about Cymbalis's loudness; it was 0.2). At 0 dB level, centre pan, full-velocity head hits peak around -8 dBFS and measure -20 to -30 LUFS momentary (Cymbalis's Nova Ride, a full bow hit: -25); a kick and snare groove at velocity 100 measures -26.5 LUFS integrated (Cymbalis's ride pattern at velocity 100: -28.5).

Four tom rim shots peaked far above everything else (Tom 3's at -1.4 dBFS before the raise), so they have fixed level trims (`kRowTrims` in `KitMap.h`, applied in the engine to everything those strikes make): Tom 2 rim -5.5 dB, Tom 3 rim -8.0, Tom 5 rim -2.5, Tom 6 rim -3.5. With them no velocity-127 hit peaks above -3 dBFS (host check); a rim-shot roll on those toms and a busy groove with rim shots peak at -3.1 dBFS. Individual outputs come later.

## Open decisions (Kris)

- Individual outputs (later)
- Plugin code and final name

## Checks (`tympanis_hosttest checks`, run in CI)

The baked kit loads; 122 parameters, none for the kicks' strike position; the Snares switch on the Snare and the Popcorn Snare only (default on), no Snare 2 or 3 parameters, Snares Off plays the wires-off model, and flipping the switch leaves a ringing hit as it is; every row plays on its default note; another channel plays nothing; host block sizes 1..4096 give the same level in every 100 ms window; saved state restores parameters, channel and notes (including a row set to none); a note moved to another row, and a saved state with a shared note; level (-inf silences the drum), pan (hard left: right silent, +3 dB left), main volume; Size is the shell's depth (Tom 3: 10", 6 .. 12" in 1" steps; Tom 1: 10"; Octoban 4: 23.5"; the Popcorn Snare: 5"), a shallower Tom 3 rings higher and a deeper lower, Size on the Snare changes nothing; decay lengthens the ring, strike near the rim is brighter; the tail falls silent (below -94 dBFS after 29 s); headroom (every row's velocity-127 hit peaks below -3 dBFS); velocity sensitivity (default 100%; at 0% velocities 1 and 127 play the same, as velocity 100; 50% narrows 127 vs 40; per drum; the editor sees the velocity played); the editor's hits (one event per strike, row and velocity; a full FIFO drops, never blocks); MIDI learn (the next note on the channel, other channels ignored); the editor's page, drum and size restored; the editor opens at the saved size and adds no parameters.
