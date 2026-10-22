# 07: Materials on the asset store: overrides over shader defaults

**What to build:** `MaterialData` exists as a shared asset of overrides over its shader's parameter defaults, created, cloned, set, read and reset through the store so C++ and C# agree (spec: "FenrirAssets" (material edits), "Materials and lights" (`MaterialData`), "Testing Decisions" seam 4 (material overrides); ADR-0024, ticket 41). Drawing with it comes in tickets 11 and 13.

- **`MaterialData`:** a shader (`AssetHandle<Shader>`), a `BlendMode` (`Opaque`, `Masked` with `alphaCutoff`, `AlphaBlend`, `Additive`), `doubleSided`, and a name → value table of overrides only. Values are `float`, `vec2`, `vec3`, `vec4`, colour, or a texture (`AssetHandle<Image>` plus a `SamplerDescription`: wrap, filter, anisotropy; default repeat, trilinear, anisotropy 8).
- **Create and copy:** `CreateMaterial(AssetHandle<Shader>, AssetOwner)`; `CloneMaterial(m, AssetOwner)` copies overrides, not resolved defaults.
- **Set** (each bumps the revision): `SetFloat`, `SetVector2/3/4`, `SetColour`, `SetTexture(m, name, image, sampler)`, `SetBlendMode` with `SetAlphaCutoff`, `SetDoubleSided`. An unknown name or wrong type is logged once per material and ignored.
- **Read:** `TryGetFloat/Vector2/3/4/Colour/Texture(m, name)` returns the effective value (override, else the shader's default), empty only for a name the shader lacks or a wrong type; `TryGetOverride…` returns only an override; `IsSet`. Reads never bump the revision.
- **`Reset(m, name)`** removes an override and bumps the revision only when it removed something.
- An override the shader lacks (after the shader changes) is kept, logged once and ignored; unset parameters follow a changed shader's defaults.

**Blocked by:** 06 (FenrirShaderCompiler and the shader container).

**Touches:** `packages/FenrirAssets/`

**Status:** resolved

- [x] `FenrirAssetsTests` materials cases, on a fixture shader container: a new material's `TryGet…` reads the shader's defaults and `IsSet` is false; a set makes `IsSet` true and `TryGetOverride` return it; `Reset` restores the default.
- [x] Revisions: each set bumps it; reads never do; `Reset` bumps it only when an override existed.
- [x] `CloneMaterial` copies overrides, and a clone of an unset parameter still follows the shader's default.
- [x] An unknown name and a wrong type are each logged once per material and ignored; `TryGet` on them is empty.
- [x] Blend mode, alpha cutoff, double-sidedness and texture samplers round-trip.
