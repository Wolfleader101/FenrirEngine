# 10: Deferred `Instantiate` and `Unpack`, `ScriptInstanceResult`, and a public `AssetStore::FindInInstance`

**What to build:** C# places prefabs into the active scene, unpacks them, and finds entities inside them (ADR-0080, ADR-0029). See the spec's "Scenes from C#".

- **`AssetStore::FindInInstance(Scene&, Entity root, id) -> Entity`.** A new public door that promotes the internal lookup in `Envelope.hpp`, with C++ tests.
- **Natives:**
  - `Instantiate(path, parent = Entity.None) -> ScriptInstanceResult`. It builds in a scratch scene, replays through the change buffer, and returns the reserved root and the counts.
  - `Unpack(root) -> bool`, deferred the same way.
  - `FindInInstance(root, sourceId) -> Entity`, which gives `Entity.None` when absent.
- **`ScriptInstanceResult`** is a flat, described shared struct holding the status, the counts and `root`.

**Blocked by:** 09.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, `src/SceneReader.cpp` or `src/Envelope.*`, `Tests/`), `packages/FenrirAssets/Scripting/` (`ScriptAssets.hpp`, `src/AssetsScripting.cpp`, `.api.toml`, `Tests/`)

**Status:** ready-for-agent

- [ ] `Instantiate` from inside a system places the instance after the change buffer replays, and `FindInInstance` finds an inner entity by its source id.
- [ ] `Unpack` from C# unlinks the instance, and a later `SaveScene` writes it verbatim.
- [ ] C++ `FindInInstance` finds entities in nested instances, and gives an empty entity for an unknown id.
