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

**Status:** ready-for-agent

- [ ] `fenrir-describe-api` and `fenrir-generate-bindings` leave the tree clean; no `IRIS_FIELD`, `EnumMapping`, `[NativeFunction]` or `NativeFunction::Make` remains under the package except the five handle mappings.
- [ ] The existing assets extension tests pass unchanged in behaviour (loading, meshes, images, materials, shaders, no-op rules), with the generated defaults and natives tests in the test target.
- [ ] `Assets.Load<Mesh>("…")`, `Materials.SetColour(material, "tint", Colour.Red)` and `Shaders.GetParameter` read exactly as before from a script.
