# 11: FenrirScripting's README and Fenrir.Assets' docs, and ScriptingDemo loading a C# data asset and a scene from C#

**What to build:** a C# game developer can learn the new doors from the docs and see them run (ADR-0083).

- **FenrirScripting's README** covers:
  - `IDataAsset`, the attributes and their command names;
  - what a C# component reports now;
  - admission of data asset types;
  - what survives a reload.
- **FenrirAssets' README** covers the C# side:
  - `Load<T>`, `TryGet<T>`, `Replace<T>`, `Add<T>` and `Save` for data assets, and why there is no `Edit`;
  - the scene doors, which ones are deferred, and their result structs.
- **ScriptingDemo**:
  - defines a C# data asset type, loads it from a `.asset.json5`, and shows a value from it;
  - loads a scene file from C# and activates it with `Scenes.SetActive`;
  - instances a prefab from C# under a parent.

  Its smoke run covers the new paths.

**Blocked by:** 08, 10.

**Touches:** `packages/FenrirScripting/README.md`, `packages/FenrirAssets/README.md`, `examples/ScriptingDemo/` (scripts, assets and smoke checks)

**Status:** ready-for-agent

- [ ] Both READMEs describe the new doors, matching the code.
- [ ] ScriptingDemo loads a C# data asset and a scene from C#, and instances a prefab, in its smoke run.
