# Mac builds

`.github/workflows/build-macos.yml` builds Tympanis on GitHub's macOS runners (macOS 15, Xcode 16.4, Apple M1): a universal (arm64 + x86_64) VST3 and AU for macOS 11 and later.

## When it runs

| Event | Result |
|---|---|
| A pull request (any branch) | Build, checks, zips as the run's artifact |
| A push to `main` | The same, for `main` |
| Run workflow (Actions tab, any branch) | The same, by hand |

Changes to `docs/` or Markdown files alone do not start a build.

Each run: configures and builds (Ninja, ccache, JUCE cached), runs the processor checks (`tympanis_hosttest checks`) and the CPU benchmark, prints the editor's drawing times, checks both architectures, macOS 11.0 minimum and no AVX in the x86_64 slice, signs ad hoc, validates the VST3 with pluginval and the AU with auval (native and under Rosetta), and uploads `Tympanis-VST3-macOS-<commit>.zip` and `Tympanis-AU-macOS-<commit>.zip` as the artifact `Tympanis-macOS-<commit>` (kept 30 days). A Linux job checks the locked files (`docs/locked.md5`), that the committed bake matches `tools/bake_kit.py`, and that the C++ engine matches the Python prototype (`tools/ref_check.py`).

## Getting a build

GitHub → Tympanis → Actions → Build macOS (VST3 + AU) → the run for the branch or pull request → Artifacts at the bottom of the run's page. Signed in to GitHub, the artifact downloads as a zip holding the two zips.

No releases: Tympanis is private and copyrighted (Kris, 2026-11-24). The workflow publishes nothing; builds exist only as run artifacts, for 30 days.

## Installing a CI build

1. Unzip.
2. Move `Tympanis.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `Tympanis.component` to `~/Library/Audio/Plug-Ins/Components/`.
3. Clear the download quarantine flag once per build, because the builds are not notarized yet:

   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Tympanis.vst3
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Tympanis.component
   ```

4. Rescan plugins in your DAW. For Logic, you may need to run `killall -9 AudioComponentRegistrar` first.

## Local build (Mac)

```
export MACOSX_DEPLOYMENT_TARGET=11.0
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --target Tympanis_VST3 Tympanis_AU
```

## The packed kit

The JSON bakes in `resources/bake/` are the source (`tools/bake_kit.py` writes them). The plugin embeds each one in a packed form instead (in its binary, through BinaryData): when CMake builds the plugin's binary data, `tools/pack_bake` packs every bake into `<build>/bake/<key>.bake.pack`. A packed bake (`plugin/PackedBake.h`) is Cymbalis's packed bake, format 1, with Tympanis's own magic (`TYMPPACK`): the bake's JSON tree in binary, compressed with zlib, every number as its exact double (or int32 where it is exactly one). It carries a format version and an FNV-1a checksum; a packed bake that does not match them fails to load with a message. The tool checks each packed bake before writing it: it must unpack to the same JSON, bit for bit, and load (the kit header, or a drum), or the build stops.

Same sound: renders through the processor give the same samples, bit for bit, as with the gzipped JSON (2026-11-25: md5s of the raw floats of the busy stereo sequence at 64 and 256-sample blocks).

| Kit (17 drums and the header) | Bytes |
|---|---|
| JSON | 14.0 MB |
| gzipped JSON (before) | 4.55 MB |
| packed | 3.80 MB |

Every copy of the plugin's code carries the kit once: the universal VST3 and AU have two each (arm64 and x86_64), so packing saves about 3 MB over the two. Loading the kit when the plugin opens went from about 200 ms to about 65 ms (Linux build, `tympanis_hosttest`). Most of the rest is the baked strike-position and mode-shape grids (about 3.0 of the 3.8 MB), which could be computed when the plugin loads instead.

## Plugin identity

| Field | Value |
|---|---|
| Company | DHammers |
| Product name | Tympanis (working name) |
| Manufacturer code | `Dham` |
| Plugin code | `Tymp` (working choice) |
| Bundle ID | `com.dhammers.tympanis` |
| AU type | `aumu` (instrument) |

Not notarized: builds are for Kris's own machines (the quarantine step above).

## JUCE splash screen

The plugin is built with `JUCE_DISPLAY_SPLASH_SCREEN=0` (Kris, 2026-11-25, as Cymbalis): no "Made with JUCE" logo when the editor opens. Whether the JUCE licence in use allows that is Kris's to settle.

## dhpool (the shared job pool)

`third_party/dhpool/` is a copy of Cymbalis's `shared/dhpool/` (github.com/DustyHammers/Cymbalis), the job pool both plugins, and the kit that merges them, render through. `third_party/dhpool/SOURCE.md` records the commit it was copied at (`5ef9885`, 2026-11-26) and its version (2). The build adds it as its own target (`dhpool`, linked by the engine) with its self-test `dhpool_selftest`, which CI runs on the Mac and on Linux.

**It is never edited here.** A change is made in Cymbalis; then the whole folder is copied again at the new Cymbalis commit (byte for byte, nothing else from Cymbalis) and `SOURCE.md` updated, in a pull request of its own, and the checks (the same bits at 1-3 threads, ThreadSanitizer) run on it.
