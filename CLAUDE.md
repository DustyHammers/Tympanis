# Tympanis — rules for Claude Code

Tympanis (working name) is a physically modelled drum instrument (macOS VST3 + AU, JUCE 7.0.12): the TAMA Starclassic Maple Simon Phillips kit as modal resonator models derived from the Clavia ddrum4 SP sounds. It is built like Cymbalis (github.com/DustyHammers/Cymbalis) and may later be merged with it. **No samples: no recorded audio anywhere in the engine or the models.** Kris judges the sound by ear.

## Standing rules

- No recorded audio in the engine, the models or the plugin. Every transient and noise is modelled.
- Never add or remove plugin parameters without explicit instruction from Kris.
- Never modify `prototype/engine.py` or `resources/models/*.json`. They are locked (`docs/locked.md5`). Re-bake with `tools/bake_kit.py` only when the bake tool itself changes.
- Always confirm the baseline file (md5) before editing an engine or model file.
- Always show Kris a mockup before building any custom UI. The editor follows the approved mockups (`docs/UI.md`).
- One change per task. Work on a branch and open a pull request; do not push to `main` directly.
- Renders for Kris are engine/plugin output only; never play him the original ddrum4 recordings unless he asks. Audio files under 20 MB (MP3 320k + 16-bit WAV). No audio in the repo, CI or pull requests.
- Real-time requirement: the plugin stays at least 3x faster than real time at 44.1/48 kHz in `tympanis_hosttest bench`.
- Report anything in the engine or models that looks wrong; don't change sound behaviour unless the task asks for it.
- There is no `README.md` for now. Put documentation in `docs/`.

## Repo map

| Path | Contents |
|---|---|
| `prototype/engine.py` | Python engine (md5 735fd0ea), the reference the C++ engine was ported from |
| `resources/models/` | The 17 locked kit models (modal data per articulation and velocity layer) |
| `resources/bake/` | Baked kit for the C++ engine (`tools/bake_kit.py`), gzipped into the plugin |
| `plugin/dsp/` | C++ engine, no JUCE (`Engine.h`, `KitData.h`, `Filters.h`, `Json.h`) |
| `plugin/` | JUCE processor (`PluginProcessor.*`), editor (`PluginEditor.*`), drums and note rows (`KitMap.h`) |
| `plugin/ui/` | Editor assets loader, wireframe drums (`DrumModel`), their rasteriser (`WireRaster`) and view (`WireView`) |
| `resources/ui/` | Editor background, knob filmstrip, fonts (OFL) |
| `tools/tympanis_render/` | Console renderer for the engine (port check, CPU) |
| `tools/tympanis_hosttest/` | Drives the processor like a host: checks, CPU bench, renders through processBlock |
| `tools/tympanis_snapshot/` | Renders the editor's pages to PNG, prints the wireframe drawing times |
| `tools/ui/make_background.py` | Makes `resources/ui/TYMPANIS_Background.png` |
| `tools/ref_check.py` | C++ vs Python port check (numbers only) |
| `docs/ENGINE.md` | Engine, bake, port notes, checks |
| `docs/PLUGIN.md` | Parameters, MIDI map, output, open decisions |
| `docs/UI.md` | Editor: pages, assets, wireframes, hits |
| `docs/BUILDS.md` | Mac builds in CI: triggers, artifacts, releases, installing |
