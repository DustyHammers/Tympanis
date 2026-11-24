# Plugin

macOS VST3 + AU instrument (JUCE 7.0.12), universal arm64 + x86_64, macOS 11+. Identity: company DHammers, manufacturer code `Dham`, plugin code `Tymp`, bundle ID `com.dhammers.tympanis` (code and name are working choices).

## Host parameters (35)

| ID | Name | Range | Default |
|---|---|---|---|
| `<drum>_tune` | `<Drum> Tune` | -2 .. +2 semitones | 0 |
| `<drum>_lug_detune` | `<Drum> Lug Detune` | 0 .. 1 (1 = 5 % RMS tension error between lugs) | 0 |
| `snare_buzz` | Snare Buzz | 0 .. 1 | 0.5 |

`<drum>` is each of the 17 drums: `kick1 kick2 snare1..snare4 tom1..tom6 gong octo1..octo4`. Re-strike damping and hit-to-hit variation are hardwired. The engine's other settings (decay, glide amount, click and noise mix, velocity range, strike position, size, tube length) are not exposed; velocity range is 24 dB.

## MIDI map (session state, not parameters)

As in Cymbalis: one MIDI channel for the plugin and one note per row, saved with the session. Every articulation is its own row; rows of one drum play its one shared bank. Note-offs are ignored (drums ring on). If two rows have the same note, both play.

| Row | Default note | Row | Default note |
|---|---|---|---|
| Kick 1 | 36 | Tom 1 / rim shot | 50 / 72 |
| Kick 2 | 35 | Tom 2 / rim shot | 48 / 73 |
| Snare 1 / rim shot / side stick | 38 / 40 / 37 | Tom 3 / rim shot | 47 / 74 |
| Snare 2 / rim shot / side stick | 24 / 25 / 26 | Tom 4 / rim shot | 45 / 75 |
| Snare 3 / rim shot / side stick | 27 / 28 / 29 | Tom 5 / rim shot | 43 / 76 |
| Snare 4 / rim shot / side stick | 30 / 31 / 32 | Tom 6 / rim shot | 41 / 77 |
| Gong Drum | 64 | Octoban 1..4 | 60..63 |

The defaults are a **placeholder** (GM-like where GM has the sound, the rest on free notes away from GM's cymbal notes); default channel 10 is a placeholder too. The channel dropdown and note boxes need the UI, which waits for an approved mockup; until then the plugin shows JUCE's generic parameter editor and the map stays at its defaults (it is saved and restored already).

## Output

Stereo (or mono) out, the mono kit on both channels. Fixed output gain 0.2: full-velocity head hits peak around -13 dBFS; the loudest single hit, tom 3's rim shot, about -2 dBFS.

## Open decisions (Kris)

- Default note layout, default MIDI channel (10 or 16), whether rows may share a note
- Which other engine settings, if any, become parameters
- Output format: one stereo mix, or multi-out (kick / snares / toms ...)
- Plugin code and final name
- UI (mockup first)

## Checks (`tympanis_hosttest checks`, run in CI)

The baked kit loads; 35 parameters; every row plays on its default note; another channel plays nothing; host block sizes 1..4096 give the same level in every 100 ms window; saved state restores parameters, channel and notes (including a row set to none); the tail falls silent.
