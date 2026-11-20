# 03: describe-api: the `entity`, `fixed-string` and `reference` kinds, read by Iris.Bindings, every description and binding regenerated

**What to build:** the three fixed-layout kinds files need from components (ADR-0078): `Fenrir::Entity` is recorded as `entity` rather than a nested struct of a `u32` and a `pointer`, `char[N]` as `fixed-string` rather than an `array` of `u8`, and `Fenrir::AssetHandle<T>` as `reference` with its target type rather than a bound type matched by layout. Iris.Bindings reads the three and generates the mirrors it generates today, so every binding regenerates byte for byte, and every component holding one of them (the nine `Entity` fields, `Name`, `BoneName`, every `AssetHandle<T>` in a renderer or skeleton component) describes with the new kind.

- **`entity`:** the tool recognises `Fenrir::Entity` by qualified name (the tool stays binding-agnostic but not engine-agnostic, the types spec's gap); 16 bytes, no element keys. `Fenrir::Entity` itself stays a `shared-struct` in FenrirECS's selection so the built-in binding's mirror is still generated, and the other selections drop it from their `private-members` lists where they named it.
- **`fixed-string`:** `char[N]` (and `char8_t[N]`) becomes `fixed-string` with `length` N, UTF-8 text cut at N-1 bytes on a boundary when read back (the serialiser's rule, not the tool's); `std::uint8_t[N]` stays an `array` of `u8`. `Name::name` and `BoneName`'s buffer are the two in the engine.
- **`reference`:** `Fenrir::AssetHandle<T>` is recorded as `reference` with `target` the canonical name of `T` (an asset kind's short name for an imported asset, `Fenrir::Mesh`; a described data asset type's name later); 8 bytes, the two fields a bound type recorded. The `[bound-types]` entries for the handle specialisations stay for Iris.Bindings' mapping until the C# spec removes them.
- **Iris.Bindings** maps `entity` to the built-in binding's `Entity`, `fixed-string` to the `fixed byte` buffer it made from the `array` of `u8` (with the same `Set` helpers), and `reference` to the bound handle it already names for that target, so no generated C# changes; its tests gain one case per kind.
- **Regeneration:** every `.api.json` and every generated binding is regenerated and committed; the only description changes are the three kinds where the engine has them.
- **`SCHEMA.md`** and Iris's copy document the three kinds in the "Kinds" table, with the 16, N and 8 bytes.

**Blocked by:** 02.

**Touches:** `tools/describe-api/src/` (`Describer.cs`, `Description.cs`, `DescriptionWriter.cs`), `tools/describe-api/SCHEMA.md`, `tools/describe-api/tests/`, `packages/Iris/Tools/Iris.Bindings/` (the kind mapping, its tests and fixture), every `packages/*/api/*.api.json` and `*.api.toml` (`private-members` lists), the generated files under `packages/*/Scripting/src/Generated/` and `packages/*/Managed/*/Generated/` where they regenerate

**Status:** resolved

- [x] The fixture header gains a component with an `Entity`, a `char[32]` and an `AssetHandle<Mesh>` field; `Fixture.api.json` records `entity`, `fixed-string` with `length` 32, and `reference` with `target`, compared byte for byte.
- [x] `FenrirPhysics.api.json` records `Joint.bodyA` as `entity`, `FenrirECS.api.json` records `Name.name` as `fixed-string`, `FenrirRenderer.api.json` records `MeshRenderer`'s mesh as `reference` to `Fenrir::Mesh`.
- [x] `fenrir-generate-bindings` leaves every generated C# file and native mapping byte-identical, and the scripting tests pass unchanged.
- [x] Iris.Bindings' tests cover the three kinds' mappings.
- [x] `std::uint8_t[N]` still describes as an `array` of `u8`.
