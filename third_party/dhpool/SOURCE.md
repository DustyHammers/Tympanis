# dhpool: where this copy comes from

- **Repository:** github.com/DustyHammers/Cymbalis, folder `shared/dhpool/`
- **Commit:** `cf9aad1dbb5b3415bf8a316181becb40cc5059cc` (Cymbalis `main`, PR #75, "Shared job pool dhpool, EnginePool on top: multi-threading step 2"), 2026-11-25
- **Pool version:** 1 (`dhpool::kVersion`)
- **Copied:** 2026-11-25, unchanged (every file byte for byte as at that commit). This file is the only one added.

**The rule: don't edit it here.** Cymbalis owns dhpool. A fix or a change is made in Cymbalis, then the whole folder is copied here again at the new commit, and this file updated (`docs/BUILDS.md`, "dhpool"). That way Tympanis, Cymbalis and the kit plugin that merges them run the same, tested pool.

How to use it: `README-pool.md`. In Tympanis: `Kit::setPool` (`plugin/dsp/Engine.h`, `docs/ENGINE.md`, "Kit").
