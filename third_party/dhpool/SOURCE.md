# dhpool: where this copy comes from

- **Repository:** github.com/DustyHammers/Cymbalis, folder `shared/dhpool/`
- **Commit:** `5ef9885b463ddaf1d654f1571eddca73cc21a30e` (Cymbalis `main`, PR #78, "dhpool self-test: settle the thread count in the prepare / release check (dhpool 2)"), 2026-11-26
- **Pool version:** 2 (`dhpool::kVersion`)
- **Copied:** 2026-11-26, unchanged (every file byte for byte as at that commit). This file is the only one added.
- **History:** version 1 copied 2026-11-25 at `cf9aad1` (PR #75). Version 2 changes only the self-test (the prepare / release check reads the thread count once it has settled; it had started failing on the Mac CI runner), the version number and README-pool.md's version line; the pool itself is the same.

**The rule: don't edit it here.** Cymbalis owns dhpool. A fix or a change is made in Cymbalis, then the whole folder is copied here again at the new commit, and this file updated (`docs/BUILDS.md`, "dhpool"). That way Tympanis, Cymbalis and the kit plugin that merges them run the same, tested pool.

How to use it: `README-pool.md`. In Tympanis: `Kit::setPool` (`plugin/dsp/Engine.h`, `docs/ENGINE.md`, "Kit").
