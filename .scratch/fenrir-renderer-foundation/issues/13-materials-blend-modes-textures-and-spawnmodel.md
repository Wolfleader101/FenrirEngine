# 13: Materials in full: blend modes, textures, user shaders and SpawnModel

**What to build:** Every material the spec describes draws correctly: opaque, masked, alpha-blended and additive, double-sided, textured with their samplers, or on a user's own GLSL program, and an imported model spawns as drawn entities with one call; RendererDemo's "Meshes and materials" scene shows it all (spec: "Materials and lights" (render state, built-ins, default textures, variants, user shaders, samplers, pipelines), "FenrirRenderer: the frame" (transparent sorting, the `Transparent` pass), "FenrirRenderer: the ECS facade" (`SpawnModel`), "Demo apps" (RendererDemo scene 1); ADR-0024, ADR-0023, ADR-0031, ticket 41).

- **Render state:** `Opaque` and `Masked` draw in `Opaque` with depth write; `AlphaBlend` and `Additive` in `Transparent` (depth tested, no write, sorted back to front by the view-space depth of the item's world origin; no OIT, documented). `doubleSided` turns culling off.
- **Variants:** the renderer picks the keys; `ALPHA_MASK` from `Masked` (with `alphaCutoff`); `SKINNED` reserved and never set; a user shader's own keys stay off.
- **Textures:** the built-ins' texture parameters (`baseColourTexture`, `metallicRoughnessTexture`, `normalTexture`, `emissiveTexture`) sampled; a missing texture binds its default (`white`/`normal`/`white`; `emissiveTexture` was `black` until this ticket amended it) instead of adding a key; mips made at upload; samplers per parameter, deduplicated by description in the core.
- **User shaders:** full vertex and fragment programs on the engine includes with their own set 2, built by `fenrir_add_shaders` and loaded as `ShaderData`; lighting is the shader's choice. Pipelines keyed by program and variant, render state, vertex layout, target formats and MSAA; cached while the shader asset lives. Materials referenced at load may be prepared early (spike lesson 8).
- **`SpawnModel(scene, AssetHandle<Model>, parent = {}) -> Entity`:** `SpawnNodes`, then a `MeshRenderer` per mesh of each node, with a child entity per extra mesh or submesh; returns the root. Written so the animation spec can insert `SpawnSkeletons` between the two steps.
- **RendererDemo scene 1, "Meshes and materials":** the imported backpack (added to `examples/assets/` with its attribution); primitives in unlit and lit over a metallic/roughness grid; the four blend modes side by side and a double-sided quad; the magenta fallback and a stale or empty mesh; keys rotate the light, change the ambient, toggle `visible` and edit a shared material's parameter. A small user shader in RendererDemo's own sources. Its hand checks in the README.

**Blocked by:** 08 (Model import through cgltf and ufbx, and SpawnNodes); 12 (The demo scaffold and an empty RendererDemo).

**Touches:** `packages/FenrirRenderer/`, `examples/RendererDemo/`, `examples/assets/`

**Status:** resolved
Hand check: RendererDemo scene 1, "Meshes and materials" (the hand checks in `examples/RendererDemo/README.md`): the imported backpack is textured and lit; the unlit primitives are flat and the lit ones shaded over the metallic/roughness grid, which reads metallic left to right and roughness bottom to top; the four blend modes (Opaque square, Masked hard-edged disc, AlphaBlend soft disc, Additive glow) and the double-sided quad (its back shown, the single-sided one vanishing) look right; the no-material cube is magenta and the empty and stale meshes draw nothing; L turns the light, M changes the ambient, V toggles the backpack's `visible`, C changes the shared material's colour.

- [x] Null-backend tests through `RendererProbe`: masked materials use the `ALPHA_MASK` variant; transparent items draw in `Transparent` back to front; opaque and masked write depth and transparent do not; double-sided disables culling; a missing texture binds its default; two parameters with equal sampler descriptions share one sampler.
- [x] A user shader container loaded as `ShaderData` draws with its own set 2 laid out from reflection.
- [x] `SpawnModel` on the backpack fixture gives one `MeshRenderer` per mesh and submesh, under the returned root, and the probe sees each draw.
- [x] GL pixel probes: a masked quad's cut-out pixel shows the background; an alpha-blended quad over an opaque one gives the blended colour within tolerance.
- [x] `FENRIR_DEMO_SMOKE` runs the scene with its keys on null and GL with no errors.
- [ ] Scene 1 by hand: the backpack is textured and lit; the metallic/roughness grid reads correctly; the four blend modes and the double-sided quad look right; the fallback is magenta; the keys work (checked by hand).
