# Plugin

macOS VST3 + AU instrument (JUCE 7.0.12), universal arm64 + x86_64, macOS 11+. Identity: company DHammers, manufacturer code `Dham`, plugin code `Tymp`, bundle ID `com.dhammers.tympanis` (code and name are working choices).

## Host parameters (119)

Per drum (`<drum>` = `kick1 kick2 snare1..snare4 tom1..tom6 gong octo1..octo4`):

| ID | Name | Range | Default |
|---|---|---|---|
| `<drum>_tune` | `<Drum> Tune` | -2 .. +2 semitones | 0 |
| `<drum>_lug_detune` | `<Drum> Lug Detune` | 0 .. 1 (1 = 5 % RMS tension error between lugs) | 0 |
| `<drum>_decay` | `<Drum> Decay` | 0.25 .. 4 x ring time (highs change less) | 1 |
| `<drum>_strike_pos` | `<Drum> Strike Position` | 0 (centre) .. 0.95 (near the rim); not on the bass drums | 0.35 |
| `<drum>_size` | `<Drum> Size` | 0.5 .. 2 x the catalog diameter, inches | catalog size |
| `<drum>_level` | `<Drum> Level` | -inf (bottom, -60) .. +6 dB | 0 dB |
| `<drum>_pan` | `<Drum> Pan` | -1 (left) .. +1 (right), -3 dB compensated constant power (0 dB centre, +3 dB hard) | 0 |

Kit: `snare_buzz` Snare Buzz 0 .. 1 (0.5); `main_volume` Main Volume -inf .. +6 dB (0 dB), on the summed stereo output.

Decisions (Kris, 2026-11-24): decay, strike position, size, tune and lug detune exposed per drum; independent level and pan per drum; main volume on the 2-channel sum; individual outputs later. The kicks have no strike position: their beater position is fixed (as sampled) and the engine ignores it. Re-strike damping and hit-to-hit variation are hardwired; glide amount, click and noise mix, velocity range (24 dB) and tube length are not exposed.

Strike position applies to head hits and rim shots (the stick's head contact), not side sticks. Tune, decay and size act live on ringing drums; strike position from the next hit. Level and pan ramp over each rendered piece (no zipper noise). A snare's sympathetic head ring and wire buzz follow that snare's level and pan; the coupling itself uses the unfaded kit, as in a room.

Catalog sizes: kicks 22", snares 1-3 14", Snare 4 10" (Kris, 2026-11-24; its Size runs 5" .. 20"), toms 10/12/13/14", floor toms 16 x 16" and 18 x 16", gong drum 22", octobans 6". At the catalog size every drum sounds as modelled.

## MIDI map (session state, not parameters)

As in Cymbalis: one MIDI channel for the plugin (default 10) and one note per row, saved with the session. Every articulation is its own row; rows of one drum play its one shared bank. Note-offs are ignored (drums ring on). A note belongs to at most one row: giving a row a note another row has sets that row to none; a saved state where rows share a note keeps it on the first row.

| Row | Default note | Row | Default note |
|---|---|---|---|
| Kick 1 | 36 | Tom 1 / rim shot | 50 / 72 |
| Kick 2 | 35 | Tom 2 / rim shot | 48 / 73 |
| Snare 1 / rim shot / side stick | 38 / 40 / 37 | Tom 3 / rim shot | 47 / 74 |
| Snare 2 / rim shot / side stick | 24 / 25 / 26 | Tom 4 / rim shot | 45 / 75 |
| Snare 3 / rim shot / side stick | 27 / 28 / 29 | Tom 5 / rim shot | 43 / 76 |
| Snare 4 / rim shot / side stick | 30 / 31 / 32 | Tom 6 / rim shot | 41 / 77 |
| Gong Drum | 64 | Octoban 1..4 | 60..63 |

The default notes follow **General MIDI** (Kris, 2026-11-24, for now): GM's notes where GM has the sound (kicks 36/35, snare 38, side stick 37, toms 50/48/47/45/43/41; Snare 1's rim shot on GM's electric snare, 40, as most GM-compatible drum maps). The rows GM has no sound for sit below GM's range (24..32) and on GM's bongo/conga notes (60..64: gong drum, octobans) and whistle/guiro/claves/wood-block notes (72..77: tom rim shots), away from its cymbals; a GM drum part that uses those percussion notes plays these drums. The editor sets the channel (MIDI CHANNEL box) and each row's note (the DRUM page's MIDI NOTE boxes: drag, pick from the list, NONE, RESET DEFAULT or MIDI LEARN: the next note-on on the channel goes to the row and plays it).

## Editor

Three pages in Cymbalis's look (docs/UI.md): DRUM, KIT and TUNING. The page, the selected drum and the editor's size are saved with the session (not parameters).

## Output

Stereo out (mono out: (L + R) / 2). Under Main Volume a fixed gain of 0.2: full-velocity head hits peak around -13 dBFS at 0 dB level, centre pan; the loudest single hit, tom 3's rim shot, about -2 dBFS. Individual outputs come later.

## Open decisions (Kris)

- Individual outputs (later)
- Plugin code and final name
- Velocity Sensitivity per drum (to do: 17 new parameters, DRUM page bottom row VS / LEVEL / PAN)
- Snare 1 and 2 as one drum with shared settings, or two (now two)

## Checks (`tympanis_hosttest checks`, run in CI)

The baked kit loads; 119 parameters, none for the kicks' strike position; every row plays on its default note; another channel plays nothing; host block sizes 1..4096 give the same level in every 100 ms window; saved state restores parameters, channel and notes (including a row set to none); a note moved to another row, and a saved state with a shared note; level (-inf silences the drum), pan (hard left: right silent, +3 dB left), main volume; size lowers the pitch, decay lengthens the ring, strike near the rim is brighter; the tail falls silent; the editor's hits (one event per strike, row and velocity; a full FIFO drops, never blocks); MIDI learn (the next note on the channel, other channels ignored); the editor's page, drum and size restored; the editor opens at the saved size and adds no parameters.
