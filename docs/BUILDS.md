# Mac builds

`.github/workflows/build-macos.yml` builds Tympanis on GitHub's macOS runners (macOS 15, Xcode 16.4, Apple M1): a universal (arm64 + x86_64) VST3 and AU for macOS 11 and later.

## When it runs

| Event | Result |
|---|---|
| A pull request (any branch) | Build, checks, zips as the run's artifact |
| A push to `main` | The same, for `main` |
| A tag `v*` pushed (e.g. `v0.1.0`) | The same, and a GitHub Release with the two zips attached |
| Run workflow (Actions tab, any branch) | The same, by hand |

Changes to `docs/` or Markdown files alone do not start a build.

Each run: configures and builds (Ninja, ccache, JUCE cached), runs the processor checks (`tympanis_hosttest checks`) and the CPU benchmark, prints the editor's drawing times, checks both architectures, macOS 11.0 minimum and no AVX in the x86_64 slice, signs ad hoc, validates the VST3 with pluginval and the AU with auval (native and under Rosetta), and uploads `Tympanis-VST3-macOS-<commit>.zip` and `Tympanis-AU-macOS-<commit>.zip` as the artifact `Tympanis-macOS-<commit>` (kept 30 days). A Linux job checks the locked files (`docs/locked.md5`), that the committed bake matches `tools/bake_kit.py`, and that the C++ engine matches the Python prototype (`tools/ref_check.py`).

## Getting a build

GitHub → Tympanis → Actions → Build macOS (VST3 + AU) → the run for the branch or pull request → Artifacts at the bottom of the run's page. Signed in to GitHub, the artifact downloads as a zip holding the two zips. For a release: tag the commit (`git tag v0.1.0 && git push origin v0.1.0`); the run attaches the zips to a Release under Releases.

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

## Plugin identity

| Field | Value |
|---|---|
| Company | DHammers |
| Product name | Tympanis (working name) |
| Manufacturer code | `Dham` |
| Plugin code | `Tymp` (working choice) |
| Bundle ID | `com.dhammers.tympanis` |
| AU type | `aumu` (instrument) |

Not notarized yet: that needs an Apple Developer ID and secrets in the repository; to be set up when Kris wants builds for other people.
