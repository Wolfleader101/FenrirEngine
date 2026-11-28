# 06: Iris.Bindings emits data asset classes and codecs for `data-asset` role types

**What to build:** each C++ data asset type a binding selects reaches C# as a generated class over the same tree, so C# never reads native bytes (ADR-0083, ADR-0075). See the spec's "C++ data asset types in C#".

- **Stop skipping the role.** Iris.Bindings currently skips the `data-asset` role (`Description.cs`). It now emits `<generated>/<Type>.cs` for each such type, holding a `public partial class <Type> : IDataAsset`. The class:
  - extends its base's class, found through the binding's top-level `uses` key;
  - has one property per field: `string`, `List<T>`, `T?`, `AssetHandle<T>`, a path as `string`, and nested structs as their mirrors;
  - takes its defaults from the description as initialisers, and its docs from the description;
  - carries the two codecs and `[NativeDataAsset("Canonical::Name")]`;
  - registers its codecs with `DataAssetCodecs` from a module initialiser in the contract.
- **Handles.** `Fenrir::AssetHandle<T>` for a described data asset `T` maps to `AssetHandle<T>`, with no `[bound-types]` entry.
- **The defaults test.** It gains data asset types: C++ `T{}` through the bridge into the tree encoding must equal C# `new T()` through its codec, byte for byte.
- **Fixture.** Select `AssetsTest::WeaponData` and its kin in a test binding, so a real class is generated and tested.

**Blocked by:** 04.

**Touches:** `packages/Iris/Tools/Iris.Bindings/` (`src/`, its tests and goldens)

**Status:** ready-for-agent

- [ ] A selected data asset type generates a class extending its base's class, with properties, defaults and codecs.
- [ ] Its codecs round-trip a tree, and the defaults test passes byte for byte.
- [ ] A data asset `AssetHandle<T>` field maps to `AssetHandle<T>`, with no bound-type entry.
