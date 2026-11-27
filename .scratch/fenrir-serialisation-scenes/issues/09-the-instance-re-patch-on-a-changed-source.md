# 09: The instance re-patch on a changed source

**What to build:** editing a prefab updates every instance of it that is running, with each host's overrides kept (ADR-0081, ADR-0080; the spec's "Hot reload").

- **The record:** the scene resource from ticket 08 also lists every instance source the scene expanded, and the watcher watches each one.
- **On a change to a source**, every instance of it, in every scene that recorded it, is re-patched:
  - each inner entity is matched by source id against the new expansion;
  - the host's overrides are reapplied on top;
  - run-time children are left alone;
  - the host file is untouched.
- **A model source's instances** re-spawn the same way, through the `ModelSpawner`.
- **On failure:** a source that fails to read changes nothing and logs once per change.

**Blocked by:** 08.

**Touches:** `packages/FenrirAssets/` (`src/ScenePatch.cpp`, `src/AssetWatcher.*`, `src/AssetStore.cpp`, `Tests/SceneReload.cpp`)

**Status:** ready-for-agent

- [ ] Editing a scene source re-patches two instances of it in a running scene. A field one host overrode keeps its override, and the other host takes the edit.
- [ ] A run-time child added under an instance survives the re-patch.
- [ ] A rewritten model source re-spawns its instances under their roots.
- [ ] A broken source changes nothing and logs once.
