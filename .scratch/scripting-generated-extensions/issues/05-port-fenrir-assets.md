# 05: Port `Fenrir.Assets` onto the generated pipeline

**What to build:** Assets' enums, shared structs, handle layouts, native declarations and registration are generated; `Assets`, `Materials` and `Shaders` are hand-written facades over typed natives (spec: "Each port"; ADR-0043, ADR-0044).

- **Selection** (`packages/FenrirAssets/api/FenrirAssets.api.toml`, headers `AssetHandle.hpp`, `MaterialData.hpp`, `ShaderContainer.hpp`, and where `AssetOwner` lives): `SamplerDescription` and `MaterialTexture` as `shared-struct`; `AssetHandle<Fenrir::Mesh>`, `<Image>`, `<Material>`, `<Shader>`, `<Model>` as `bound-type` (describe-api instantiates each to read its layout). `BlendMode`, `SamplerWrap`, `SamplerFilter`, `ShaderParameterType`, `DefaultTexture` and `AssetOwner` are described through the structs and natives that use them.
- **Binding-owned selection** (`Scripting/api/FenrirAssetsScripting.api.toml`, header `AssetMappings.hpp` or a new `ScriptShaderParameter.hpp`): `ScriptShaderParameter` as `shared-struct`, named `ShaderParameter` in `[names]`.
- **Binding** (`FenrirAssets.binding.toml`): `[uses]` the math binding; `[bound-types]` the five handles as `AssetHandle<Mesh>` and so on (no constructor: `default` is the empty handle, all zero); `[natives] sources = ["../src/AssetsScripting.cpp"]`, classes `AssetsNative`, `MaterialsNative`, `ShadersNative` (if the generator takes one class per binding, the ticket merges them into `AssetsNative` and the facades call it); `[registration]`, `[tests]`. `AssetMappings.hpp` keeps the five hand-written handle mappings with their full generic .NET type names, and nothing else.
- **Natives:** text as `std::string_view` (kept); `AssetOwner`, `BlendMode` and the kind byte as their enums (the kind stays a `Detail::AssetKind`-style enum the facade's `AssetKind<T>` maps to); a handle still crosses as the packed `std::uint64_t`, built by the facade's `Bits`; outputs marked `@param[out]`; `GetParameter`'s name span stays a `char16_t*` + capacity. The warn-once record moves to an `AssetsScripting` state class.
- **Facades:** `Assets` (`Load<T>`, `Unload<T>`, `IsValid<T>`, `AddMesh`, `AddImage`, render targets), `Materials` and `Shaders` rewritten over the generated classes; `AssetHandle<T>`, the marker classes, `AssetKind<T>` and `AssetOwner`'s docs stay hand-written (`AssetOwner` becomes generated if its enum is described; the ticket picks whichever keeps one definition).
- **Deleted:** hand-written enums and the `SamplerDescription`/`MaterialTexture` fields (equality stays as partial parts), every `[NativeFunction]` declaration, the `IRIS_FIELD` and `EnumMapping` mappings for described types, the `extension.functions` list.

**Blocked by:** 03 (`Fenrir.Math` through a math binding, and physics's `Aabb` becomes `Bounds`)

**Touches:** `packages/FenrirAssets/api/`, `packages/FenrirAssets/Managed/`, `packages/FenrirAssets/Scripting/`

**Status:** resolved

- [x] `fenrir-describe-api` and `fenrir-generate-bindings` leave the tree clean; no `IRIS_FIELD`, `EnumMapping`, `[NativeFunction]` or `NativeFunction::Make` remains under the package except the five handle mappings.
- [x] The existing assets extension tests pass unchanged in behaviour (loading, meshes, images, materials, shaders, no-op rules), with the generated defaults and natives tests in the test target.
- [x] `Assets.Load<Mesh>("…")`, `Materials.SetColour(material, "tint", Colour.Red)` and `Shaders.GetParameter` read exactly as before from a script.

## Comments

**One natives class (settled).** The generator takes one `[natives] class` per binding, so the natives are one class,
`AssetsNative`, and `Assets`, `Materials` and `Shaders` call it. Names that were per class gain the noun:
`CreateMaterial`, `CloneMaterial`, `GetShaderParameterCount`, `GetShaderParameter`. The natives are in
`Fenrir::AssetsNatives`, and the warning messages still name the facade's function (`Materials.Create: ...`).

**`AssetOwner` is generated (settled).** C++'s `AssetOwner` is a class holding the scene, not an enum, so the binding
describes its own: `Fenrir::ScriptAssetOwner` in the new `Scripting/include/FenrirAssetsScripting/ScriptAssets.hpp`,
named `AssetOwner` in `[names]`, which the natives turn into FenrirAssets's. That leaves one definition of the C# enum, and
its docs are the header's. `ScriptShaderParameter` moved into the same header (not `AssetMappings.hpp`), public because
the generated mappings include it and FenrirRenderer's binding will include those.

**Enums the natives take but no shared struct holds.** describe-api describes an enum only through a field that uses
it, and a natives description adds nothing to an API description, so `BlendMode`, `Detail::AssetKind` and the owner
were described by nothing (`SamplerWrap` and `SamplerFilter` come through `SamplerDescription`, `ShaderParameterType`
and `DefaultTexture` through `ScriptShaderParameter`). `ScriptAssets.hpp` gives them `ScriptAssetEnums`, a struct
holding one of each, selected as a shared struct only so they are described, and the binding `exclude`s it, so C# has
the enums and not the struct. A cleaner fix is a describe-api selection of an enum on its own; 04's `CursorShape` and
06's natives may want the same.

**`AssetKind` is a public C# enum.** Iris.Bindings' `internal-structs` takes structs only, so the generated
`Fenrir.Assets.AssetKind` (Mesh, Image, Material, Shader, Model) is public, beside the internal `AssetKind<T>` that maps a
marker class to it. It is new public API, nothing a script needs. Making it internal needs `internal-structs` (or a list
of its own) to take enums.

**The natives write their outputs' defaults; the facade zeroes a failed read.** The natives test needs each output to
hold `new T()` when a native returns early, so every `TryGet*` writes C++'s `T{}` first (a `Colour`'s white, a
`MaterialTexture`'s default sampler). A failed `TryGet*` still gives `default`, as the no-op test asks and as the facade's
remarks say, because `Materials` zeroes `value` when the native read nothing.

**The warn-once record moved to `AssetsScripting`** (`src/AssetsScriptingState.hpp`), with the handle packing and the
store, handle and owner lookups. The helpers left in the source take the state first, so describe-api's natives mode
does not read them as natives. A warning is now once per Scripting given the extension, not once per process; the no-op
tests pass unchanged, and their comment says so.

**describe-api finds a selected template specialisation (outside the Touches line).** The selection named the handles
`Fenrir::AssetHandle<Fenrir::Mesh>`, as the spec says, but describe-api found a type by splitting its name on `::`, so
only an alias could reach a specialisation (glm's `Quat`). It now declares an alias in its main source for a selected name
with a template argument list and finds the type through it; the existing second parse still instantiates one nothing
else does. The fixture selects `Fixture::Pair<double>`, and the README says so. The alternative was aliases in
FenrirAssets's `AssetHandle.hpp` for the binding's sake.

**`packages/FenrirAssets/CMakeLists.txt` (outside the Touches line)** gains `fenrir_describe_api(FenrirAssets ...)`, as
FenrirPhysics's does, so the package's description is regenerated whether or not scripting is built.

**Layout tests.** `Shapes.cpp` keeps the five handles' layout case (they are mapped by hand); the shared structs' case
went, replaced by the generated defaults test, which runs in `FenrirAssetsScriptingTests` with the natives test.

