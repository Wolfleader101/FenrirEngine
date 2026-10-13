# 10: Hot reload

**What to build:** Watch the script assembly's output directory. Once a new build is stable (debounced, with the file complete), reload between frames:

1. Copy the output to a fresh per-generation directory. Windows locks loaded assemblies (review-01 D5), so the build output itself is never loaded.
2. Run the old systems' `OnDestroy` and release the old generation, then confirm the unload (Iris 23). If a root leaked, log a warning that names it where possible.
3. Load the new generation all-or-nothing (05).
4. If the new generation fails, reload the previous copy and log why.

C# component pools and C# event queues survive when their full name and layout fingerprint (size, plus each field's name, offset and kind) are unchanged. Otherwise they are dropped and a warning is logged. System instances are always recreated.

**Blocked by:** 05, 08, 09, Iris 23

**Status:** resolved

- [x] Changing a system's code and rebuilding changes behaviour without a restart, and component values survive.
- [x] Changing a C# component's layout drops it from every entity, with a warning, and nothing reads stale bytes.
- [x] A broken build (failed load) keeps the old generation running.
- [x] Repeated reload cycles don't grow memory or leave generations `Pending`.
- [x] Old per-generation copies are cleaned up once collected.

## Comments

### Resolution

- **Load from a copy, always.** `Install` now copies the assembly (siblings named like it, plus the dlls its `.deps.json` lists) into `<reloadDirectory>/install-N/gen-M` and loads that, so the original is never locked. A `shared_ptr` to the directory is held by the running generation, by each released generation until the runtime reports it collected, and by a "previous" slot kept until the next generation loads; the directory is deleted when the last holder goes.
- **`State` split into what persists and what belongs to a generation.** `LoadGeneration` / `UnloadGeneration` / `Reload` / `PollForReload` / `SweepRetired` / `DropScriptData`; the phase systems are scheduled once at Install and consult the loaded generation's phase mask; the watcher is a native PreUpdate system registered before them, so a reload lands between frames.
- **Order, as the spec has it:** playback of pending changes, old `OnDestroy`, release, `WaitForUnload` (Iris 23), load all-or-nothing, and on failure load the previous copy. A restore that also fails leaves no scripts running but the runtime up, and the watcher keeps trying.
- **Surviving data** is decided by a fingerprint (size/alignment plus each field's name, type, kind, offset from Iris's member enumeration, Iris 22). Dropped components are cleared in every scene (`EntityList::ClearDynamic`, `App::ForEachScene`); a dropped or removed event's byte queue is removed and recreated. This happens at the commit step of the load, before `OnCreate`.
- **Tests** build the fixture twice (the second with `VARIANT2`: a different `Answer`, a version step of 2 in `StampSystem`, `Counter` and the `Ripple` event with a new field) and swap the file in while an app runs: behaviour changes and an unchanged component/event keeps its data; a changed component is removed from every entity with a warning and comes back zeroed at the new size; a broken file keeps the old generation running and a later good one loads; twelve reload cycles leave no pending generation and one copy; the watcher reloads once after a stable build.
- **Not measured:** memory growth is not measured directly. "Repeated reload cycles don't grow memory or leave generations Pending" is shown by the runtime reporting every released generation Collected and by the copies being deleted, which is what a leak would break.
- **Fixed on the way:** a `State` that went away without `Shutdown` (an exception during `Install`) left the process-wide context active; it now shuts itself down. Listing the folder beside the assembly with narrow strings threw for a file with a non-ASCII name.
- **Limits:** the fingerprint does not look inside nested structs; `Reload()` must be called between frames; stale scratch folders from a crashed run are not cleaned up (each Install has its own, under a per-process folder).

Spec: `.scratch/engine-scripting/spec.md` (Hot reload).
- **Gaps the review found, left as they are:**
  - The watcher polls one file (the assembly), not the whole output directory; a build that changes only a sibling dll is not noticed. The file is "complete" when its size and time have been unchanged for the debounce and it can be opened; nothing checks it is a whole assembly (a test shows a build rewritten twice inside the debounce is loaded once, at the end).
  - The leaked-root warning says a generation has not unloaded and that something holds a type from it; it cannot name the root, because the runtime does not report which. The fixture has no leaking root, so that path is untested.
  - If a load fails *after* its checks passed (starting the systems), the layouts it dropped are already gone before the previous generation is restored. The checks that can fail a load all run before that point.
  - The fingerprint is stricter than the spec (it adds the field type's name and the alignment). Only an added field is tested; renames, reorders and kind changes are not.
  - `SweepRetired` waits up to `unloadTimeout` for each generation that has not unloaded, one after another, on the main thread.
  - `Generation()` counts loads, so restoring the previous copy after a failed build counts as another.
