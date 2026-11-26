# 08: The asset watcher: `AssetsConfig::hotReload`, data assets replaced in place, `Reloaded()`, the deleted-file warning

**What to build:** the store watches every data asset file it loaded. When one changes on disk it is replaced in place under the same handle, so whatever holds the handle sees the new value. A broken edit keeps the old value and says why (ADR-0081).

- **`AssetsConfig`** gains `hotReload` (true by default), `reloadInterval` (250 ms) and `reloadDebounce` (400 ms), matching scripting's settings. `AssetsConfig` is a `settings` type, so its description and registration regenerate, and its defaults test covers the new fields.
- **The watcher:**
  - There is one `ReloadWatcher` (ticket 07) per loaded data asset file, its stamps spread across the interval.
  - It is polled between frames on the main thread, from a sequential system at `SchedulePriority::PreUpdate`. `Assets::Install` adds that system when `hotReload` is on.
  - OS notifications are not used.
- **In-place reload:**
  - A changed file is parsed into private instances, then `Replace`d under the same handles, each sub-asset under its own, bumping revisions.
  - A failure keeps the old instances and logs the located diagnostics once per change.
  - A deleted file keeps its data with one warning, and reloads if it reappears.
  - A reference that a reload adds loads with every owner of the reloaded asset.
- **`AssetStore::Reloaded()`:** this frame's swaps as `ReloadedAsset` records (asset path, kind, untyped slot) with `Is<T>(AssetHandle<T>)`. The list clears at the next poll.

**Blocked by:** 05, 07.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, `include/FenrirAssets/Assets.hpp`, `src/Assets.cpp`, `src/AssetStore.cpp`, a new `src/AssetWatcher.cpp` and `.hpp`, `api/` and the regenerated description, registration and defaults test for `AssetsConfig`, `Tests/` (a new `HotReload.cpp` over a temporary root))

**Status:** resolved

- [x] With `reloadInterval` and `reloadDebounce` at zero, rewriting a fixture between `RunFrame` calls makes the same handle read the new value within two frames, and `Reloaded()` names it once.
- [x] A broken rewrite keeps the old value and logs its located diagnostics once. A fix after it reloads.
- [x] A deleted file keeps its data with one warning, and reloads when it reappears.
- [x] With `hotReload` off, no system is added and nothing reloads.
- [x] `AssetsConfig`'s description, registration and defaults test regenerate, and CI's dirty-tree check passes.
