# 01: `AssetKind::Data`, the envelope reader and `Load<T>` of a plain data asset, with `diagnostics` and `Partial`, over generated test types

**What to build:** the asset store loads a game-defined plain struct from a `.asset.json5` file, as ADR-0075 and ADR-0079 decide. A failed field is reported where it is in the file, and the load still succeeds in part. This is the tracer through every layer: the test package's types are described and registered by the real tools, read through FenrirSerialisation, and held in a new slot table. Sub-assets, references, the asset source seam, saving and reload are later tickets.

- **The kind** (the spec, "The data asset kind"): `AssetKind::Data`, so `AssetKindCount` becomes 11. It has one slot table, keyed by asset path and deduplicated. Owners are the app or a scene, and the asset is freed with its last owner (ADR-0021). Each record carries its value's canonical type name.
- **`AssetTraits<T>`** gains a primary template, constrained on `Described<T>` with a `DataAsset` role. `AssetData<T>` is `T`, and `kind` is `AssetKind::Data`.
  - The store keeps a reference to the `TypeRegistry` app resource, taken in `Assets::Install`.
  - `Load<T>` of a type that registry lacks fails with the new `LoadFailure::NotADataAsset`.
- **The envelope reader** is private to `FenrirAssets`. It runs these steps:
  1. `ReadTree` the file's bytes. This ticket opens the file directly; ticket 02 puts the asset source in front of it.
  2. Check `format: 1`.
  3. `Find` the `type`, so former names count. Then check `IsA(type, T)`, failing with `LoadFailure::WrongType`.
  4. `Upgrade` from `version`, with the path.
  5. `FromTree` into an instance the description's `Construct` made, passing the store's `Resolvers` (empty in this ticket) and the path.

  `sub` is ticket 03's. In this ticket a file with `sub` reads its `data` and warns that sub-assets are not yet read.
- **`LoadResult<T>`** gains `diagnostics`: the located `Diagnostic`s from `ReadTree`, `Upgrade` and `FromTree`, plus the store's own. `Status()` is `Partial` when a handle was given and `diagnostics` or `failedUnits` is non-empty, or the bridge said `Partial`. It is `Failed` when the envelope did not parse, `format` is wrong, or `type` is unknown or not assignable to `T`.
- **`TryGet<T>(handle)`:** gives the one shared, read-only instance. It is null for an empty or stale handle, or for a record whose type is not assignable to `T`, logged once per handle.
- **The test package** (the spec, "Testing"):
  - `packages/FenrirAssets/Tests/Package/` holds a header with `WeaponData` and `Sword : WeaponData`. They have a `string`, a `list`, a nested struct, an `AssetHandle<Image>` and an `AssetHandle<WeaponData>` field, and no `entity`.
  - It has its own `.api.toml` with `[registration]`, run through `fenrir_describe_api` and `fenrir_generate_type_registry`, with the description and registration committed, as FenrirTypes' `Tests/Package/` is.
  - Its fixtures under `Tests/fixtures/data/`: a sword; a file at an old `version` with a registered upgrader; a file with a syntax error; a file whose `type` is not a `WeaponData`; a file with an unknown field and a wrong-typed one.

**Blocked by:** none.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetHandle.hpp`, `AssetStore.hpp`, `src/AssetStore.cpp`, `src/AssetStoreState.hpp`, a new `src/Envelope.cpp` and `.hpp`, `CMakeLists.txt` (linking `FenrirSerialisation` `PUBLIC`), `Tests/` (new `DataAssets.cpp`, `Tests/Package/`, `Tests/fixtures/data/`, `Tests/CMakeLists.txt`))

**Status:** ready-for-agent

- [ ] `Load<Sword>` of the sword fixture gives a handle whose `TryGet` reads every field, and a second load gives the same handle.
- [ ] `Load<WeaponData>` of the sword gives a handle, while `Load<Sword>` of a plain `WeaponData` file fails with `WrongType`.
- [ ] A load of a type the registry lacks fails with `NotADataAsset`.
- [ ] A file with an unknown and a wrong-typed field loads `Partial`, with both diagnostics carrying their line and column, each logged once.
- [ ] A file at version 1 loads through its upgrader to the type's version.
- [ ] A syntax error fails with one located diagnostic.
- [ ] The test package's description and registration regenerate byte-identical under CI's check.
- [ ] Owners free a data asset with its last owner, as `Owners.cpp` checks for other kinds.
