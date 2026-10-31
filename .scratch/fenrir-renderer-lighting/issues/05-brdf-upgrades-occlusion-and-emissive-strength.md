# 05: `lit` gains height-correlated Smith, multi-scatter compensation, specular anti-aliasing, `occlusionTexture` and `emissiveStrength`

**What to build:** The BRDF moves past LearnOpenGL's, and the material gains what glTF's showcase models need (spec: "Materials"; ADR-0048).

- **Height-correlated Smith visibility** replaces the separable Schlick-GGX term, for direct and image-based light.
- **Multi-scatter energy compensation** from the BRDF lookup table.
- **Specular anti-aliasing** widening roughness by the normal's screen-space variance.
- **`occlusionTexture`** (red channel, white by default) darkening indirect light only, and **`emissiveStrength`** (1 by default), both read by the glTF importer (`KHR_materials_emissive_strength`).

**Blocked by:** 02, 04

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirAssets/`

**Status:** ready-for-agent

- [ ] CPU unit tests: the visibility term against reference values; a white furnace test showing rough metals keep their energy with compensation.
- [ ] GL pixel probes: `occlusionTexture` darkens a sphere under the sky but not under the sun alone; `emissiveStrength` scales emissive linearly.
- [ ] Importer tests: a fixture's occlusion texture and emissive strength reach `MaterialData`.

Hand check: the rough-metal row of the sphere grid no longer darkens at high roughness; normal-mapped highlights do not shimmer when moving.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
