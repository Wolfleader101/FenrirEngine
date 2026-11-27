# 11: `Assets::Move` rewrites the asset paths scene files hold

**What to build:** moving a prefab, model or data asset never breaks the scenes that name it (ADR-0076; the spec's "Moving a file that scenes name"). The user added this ticket on 2026-11-27.

- **`Assets::Move` scans each `.scene.json5` file under the root**, as it scans `.asset.json5` envelopes. In each file, it reads:
  - each block's `instance` string;
  - each asset path held by an asset handle field of a component or resource entry, and of an `overrides` entry, found through the registry's descriptions.

  A path that names the moved file, or one of its sub-assets, is rewritten. Comments and every other byte are kept, through `ReadText`, the tree edit and `WriteText`.
- **`MoveResult::unscanned`** lists two kinds of file:
  - a scene file that does not parse;
  - a scene file with an entry whose type the registry lacks. The file's other entries are still rewritten.
- **`tools/move-asset`:** the smoke run gains a scene fixture that instances the moved file.

**Blocked by:** 06.

**Touches:** `packages/FenrirAssets/` (`src/Move.cpp`, `include/FenrirAssets/Assets.hpp`'s `Move` doc, `Tests/Move.cpp`, its fixtures), `tools/move-asset/tests/` (the smoke run's root and `SmokeRun.cmake`)

**Status:** ready-for-agent

- [ ] Moving a prefab that two hosts instance, and that a component's asset handle field names, rewrites all three. Every other byte in those files stays the same.
- [ ] A dry run reports the same files and writes nothing.
- [ ] After the move, each host loads with its instance expanded.
- [ ] The move-asset smoke run passes with its scene fixture.
