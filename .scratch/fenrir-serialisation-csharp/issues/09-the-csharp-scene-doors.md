# 09: The C# scene doors: `LoadScene`, `CopyScene`, both `SaveScene`s, and a deferred `LoadSceneInto`

**What to build:** C# loads, copies, saves and merges scene files through the `Assets` facade, beside the scripting-reach spec's `Scenes` facade (ADR-0079, ADR-0082, ADR-0029). See the spec's "Scenes from C#".

- **`@facade Assets` natives**, each reaching the App through the native guard's context:
  - `LoadScene(path, name) -> ScriptSceneLoadResult` creates and fills the named scene. The caller activates it with `Scenes.SetActive`.
  - `CopyScene(name) -> bool` copies the active scene.
  - `SaveScene(path)` and `SaveScene(root, path)` return `ScriptSceneSaveResult`. Ids are minted through the change buffer.
  - `LoadSceneInto(path, parent = Entity.None) -> ScriptSceneLoadResult` builds in a scratch scene and replays through the change buffer (`RecordCall`), as `Renderer.SpawnModel` does.
- **Result structs.** `ScriptSceneLoadResult` and `ScriptSceneSaveResult` are flat, described shared structs in `ScriptAssets.hpp`. They hold:
  - the status, as `LoadStatus` or `SaveStatus`, described in `FenrirAssetsScripting.api.toml`;
  - the counts.

  Diagnostics and roots are not crossed.
- **Before starting:** the scripting-reach spec's `Scenes` facade must be on main. If it is not, stop and tell the user.

**Blocked by:** 03.

**Touches:** `packages/FenrirAssets/Scripting/` (`include/FenrirAssetsScripting/ScriptAssets.hpp`, `src/AssetsScripting.cpp`, its `.api.toml` and binding configuration, the fixture, `Tests/`)

**Status:** ready-for-agent

- [ ] `LoadScene` from C#, then `Scenes.SetActive`, gives the same entities and counts as C++ over FenrirAssets' scene fixtures.
- [ ] `SaveScene` from C# writes the same bytes C++ does, with a C# component written by canonical name.
- [ ] `LoadSceneInto` from inside a system applies after the change buffer replays, and gives its counts.
- [ ] `CopyScene` makes a named copy.
