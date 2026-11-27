# 03: The scene reader: `LoadScene`, the two-pass load, `SceneLoadResult`, unknown components kept and written back

**What to build:** a `.scene.json5` file is read back into a new scene within one frame, and an unknown type in it survives a save (ADR-0078; the spec's "Loading and saving").

- **The scene envelope reader:** it reads through the store's `AssetSource` and checks `format` the way the data-asset envelope does. It refuses another `type`, and a path outside the root.
- **`AssetStore::LoadScene(App&, path, name = path) -> SceneLoadResult`:** it creates the scene through `App::CreateScene` and fills it in two passes.
  - Pass one runs `ReserveEntity` for every block and builds the id map.
  - Pass two walks the blocks in file order. For each block it:
    - completes the entity;
    - adds its `EntityId`;
    - adds and fills each component through `ComponentType::ForName` and `FromTree`, upgrading from the entry's `version` (1 when missing);
    - resolves `entity` fields through `readEntity` and the map;
    - appends the entity to its parent with `AddChild`.
  - `resources` are applied after the entities, through FenrirScene's named doors.
  - Asset handle fields load with the scene as owner.
- **Unknown types:** a component type the registry lacks goes into `UnknownComponents` as `EncodeTree` bytes, with one diagnostic per type per load. The writer from ticket 02 writes those entries back unchanged.
- **`SceneLoadResult`:**
  - `status`: `Succeeded`, `Partial` or `Failed`;
  - `roots`;
  - counts of entities, components, unknown components kept, fields dropped, references emptied and resources skipped;
  - the located diagnostics, each logged once.

**Blocked by:** 02.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, a new `src/SceneReader.cpp`, `src/Envelope.*`, `src/AssetStore.cpp`, `Tests/Scenes.cpp`, `fixtures/scenes/`)

**Status:** ready-for-agent

- [ ] Ticket 02's golden file loads, and saving the loaded scene gives the same bytes.
- [ ] A `ComponentWatch` drained after the load shows one add per component, and children are in the file's order.
- [ ] An unknown component type is kept with one diagnostic, and written back unchanged on save.
- [ ] An unknown id, a duplicate id, an unparsable file and the wrong `type` each give their status, counts and located diagnostics.
