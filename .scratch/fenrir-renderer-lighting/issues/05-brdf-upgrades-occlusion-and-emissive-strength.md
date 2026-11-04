# 05: `lit` gains height-correlated Smith, multi-scatter compensation, specular anti-aliasing, `occlusionTexture` and `emissiveStrength`

**What to build:** The BRDF moves past LearnOpenGL's, and the material gains what glTF's showcase models need (spec: "Materials"; ADR-0048).

- **Height-correlated Smith visibility** replaces the separable Schlick-GGX term, for direct and image-based light.
- **Multi-scatter energy compensation** from the BRDF lookup table.
- **Specular anti-aliasing** widening roughness by the normal's screen-space variance.
- **`occlusionTexture`** (red channel, white by default) darkening indirect light only, and **`emissiveStrength`** (1 by default), both read by the glTF importer (`KHR_materials_emissive_strength`).

**Blocked by:** 02, 04

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirAssets/`

**Status:** resolved

- [x] CPU unit tests: the visibility term against reference values; a white furnace test showing rough metals keep their energy with compensation.
- [x] GL pixel probes: `occlusionTexture` darkens a sphere under the sky but not under the sun alone; `emissiveStrength` scales emissive linearly.
- [x] Importer tests: a fixture's occlusion texture and emissive strength reach `MaterialData`.

Hand check: the rough-metal row of the sphere grid no longer darkens at high roughness; normal-mapped highlights do not shimmer when moving.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-31. Decisions to review:

- **`Fenrir/Brdf.glsl`**, a new include binding nothing: `FenrirDistribution` (GGX), `FenrirVisibility` (height-correlated Smith, Heitz 2014), `FenrirHammersley` and `FenrirSampleGgx`. `Fenrir/Lighting.glsl`, `builtin:brdf-lut` and `builtin:sky-specular` all include it, so the direct light, the lookup table and the prefiltered cube integrate one BRDF (the last two had their own copies of the sampling). `Core/Brdf` is its CPU mirror, as `Core/SphericalHarmonics` is for the irradiance.
- **Multi-scatter: two forms from one table.** The sky's light is Fdez-Aguera's split: the prefiltered cube times f0 A + B (single scattering), the irradiance times Fms Ems (multiple scattering, with Favg = f0 + (1 - f0) / 21), and the diffuse takes 1 minus both. The paper covers only image-based light, so direct light uses Filament's form of the same idea, the specular lobe times 1 + f0 (1 / (A + B) - 1). Both read ticket 04's table unchanged. The multiple-scattering share takes the ambient occlusion, not the specular occlusion, being diffuse-like.
- **Specular anti-aliasing takes the shading normal** (normal map included), as Tokuyoshi and Kaplanyan's paper does, with Filament's constants (variance 0.15, kernel cap 0.2); Filament itself takes the geometric normal. The shading normal filters a bumpy map's highlights too, which the hand check asks for, at the cost of picking up the derivative frame's noise. The widened roughness feeds the table lookup, the prefiltered mip and specular occlusion. Because it takes `dFdx`/`dFdy`, `FenrirLitColour` must not be called inside a branch neighbouring pixels may take differently; the README and the include say so. `lit`'s `ALPHA_MASK` `discard` before it shares the hazard its texture reads and derivative frame already had.
- **GL's flattening formula widens to eight bindings of a kind a set** (`ShaderBindingsPerSet` 4 to 8, amended in `ShaderGlBindings.hpp`): `occlusionTexture` is `lit`'s fifth texture, and set 0's shadow and ambient-occlusion textures (tickets 06 on) need room too. Binding points are 0 to 31: inside GL 4.6's minimums for texture units (80) and uniform buffers (72), but storage buffers in set 3 are now at 24 and 25, past GL's minimum of 8 shader storage bindings (they were already at 12 and 13); desktop drivers give far more. The compiler's error fixture is now nine textures.
- **`emissiveStrength` is its own parameter**, between `roughness` and the `ALPHA_MASK` variant's `alphaCutoff` in `lit`'s block (so `alphaCutoff` moved from byte 40 to 44). The glTF importer writes it (1 without `KHR_materials_emissive_strength`) and no longer folds the strength into `emissive`; FBX's emission factor is still folded into `emissive` as before.
- **`occlusionTexture`** binds at set 2 binding 5, white by default, red channel, read as linear data. glTF's occlusion `strength` (`1 + strength (occlusion - 1)`) is not read: it would need its own parameter, and showcase models leave it at 1. FBX's ambient-occlusion map is not imported.
- **Tests:** CPU (`Tests/Brdf.cpp`): the visibility term against Heitz's Lambda form and hand-worked values; a direct-light white furnace integrating the lobe on its own grid, short of 1 by itself and about 1 compensated, at roughness 0.7 and 1; the sky split for a white metal and for gold; specular anti-aliasing's widening and cap. The sky furnace with f0 = 1 sums to 1 by Fdez-Aguera's algebra whatever the table holds, so the GL test of a white rough metal sphere under a white sky (about 1) is the end-to-end check on the GPU's table. GL: occlusion darkening a sphere under a white sky to its red channel, and not under the sun alone; `emissiveStrength` at 1, 4 and 16. Importer: a new `occlusion-emissive.gltf` fixture (from `make_fixtures.py`). Null: `lit`'s eleven parameters and its fifth texture's default.
- **Golden images regenerated** (`FENRIR_UPDATE_GOLDENS=1`) where the BRDF change is expected: `sphere-grid-panorama.png` (the rough gold spheres no longer darken: multi-scatter; sphere rims shift slightly: specular anti-aliasing) and the six `tonemap-*.png` (the gold sphere is brighter, the dielectric's rim shifts). `panorama-sky.png` still matched and is unchanged.
- **msvc-x64-debug:** the whole CTest tree passed (166 tests, GL and demo smoke runs included; RTX 5080), then the renderer, shader compiler, assets and RendererDemo tests again after the review fixes.
- **Review** (`/code-review`, standards and spec): no wrong maths found. Fixed from it: the sampling helpers shared through `Fenrir/Brdf.glsl`, the anti-aliasing constants named, the CPU mirror's clamp matched to the shader's, the gold check its own test case, the white-sky test's comment saying what it can and cannot catch, `INFO` on the occlusion tests' setup, stale "as builtin:lit has" comments. Left: glTF occlusion `strength`; no GL test reads the GPU's table back against `BrdfScaleBias` (the GL furnace test covers it end to end).

Hand check still to do: the rough-metal row of LightingDemo's sphere grid no longer darkening at high roughness, and normal-mapped highlights not shimmering in motion (LightingDemo arrives with ticket 07).
