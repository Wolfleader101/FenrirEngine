# 07: The tree natives, `Load<T>` routing, the generation cache, `TryGet<T>`, `Replace<T>`, `Add<T>` and `Save`

**What to build:** C# loads, reads, replaces, adds and saves data assets of both C# and C++ types through the `Assets` facade (ADR-0083). See the spec's "The tree natives and the facade".

- **Natives** in `AssetsScripting.cpp`, declared and registered as ADR-0046 has it, over ticket 02's public by-name doors:
  - `LoadDataAsset`
  - `DataAssetTree`, which returns bytes only when the revision moved, and follows `GetShaderParameter`'s length rule
  - `ReplaceDataAsset`
  - `AddDataAsset`
  - both `SaveDataAsset`s, as `@facade Assets.Save`
- **The facade:**
  - `Assets.Load<T>` routes a `T` that `DataAssetCodecs` holds to `LoadDataAsset`.
  - `Assets.TryGet<T>` uses the generation cache, keyed by handle and revision, with one native call per check.
  - `Assets.Replace<T>` and `Assets.Add<T>` are hand-written, generic over `where T : class, IDataAsset`.
  - The cache is cleared when the generation stops.
  - There is no `Edit`.
- **Fixture.** `AssetsScriptingApp` registers FenrirAssets' test package. The fixture assembly defines a C# data asset type that extends `AssetsTest::WeaponData`.
- **No-op tests.** `Scripting/Tests/NoOps.cpp` calls the natives through `NativeCallHarness` with a stale handle, an empty span and a mis-typed load.

**Blocked by:** 02, 05, 06.

**Touches:** `packages/FenrirAssets/Scripting/` (`src/AssetsScripting.cpp`, its binding configuration and `.api.toml`, `Managed/Fenrir.Assets/` hand-written parts, `Tests/` (fixture, `NoOps.cpp`, the end-to-end tests))

**Status:** ready-for-agent

- [ ] A fixture `.asset.json5` of the C# type loads and reads from C#, and C++ `TryGet<WeaponData>` reads its base.
- [ ] A `Replace` from C# is seen from C++ as a revision bump and a changed tree.
- [ ] `Add`, then `Save`, then `Load` round-trips, and an unchanged `TryGet`–`Replace`–`Save` writes back byte-identical.
- [ ] A cache hit is one native call with no copy, and the no-op tests pass.
