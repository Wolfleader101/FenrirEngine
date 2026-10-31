---
status: proposed
---

# The renderer instances matching items automatically, and InstancedMeshRenderer draws instance lists

ADR-0019 made each draw-list item one draw, with "no automatic instancing", and ADR-0023 made each drawn mesh one `MeshRenderer`. Most engines do both of what follows: Unreal's dynamic instancing beside its Instanced Static Mesh components, Unity's GPU Instancing beside `RenderMeshInstanced`, Bevy's automatic batching. This amends ADR-0019 and ADR-0023.

## Automatic

- **Consecutive items with the same pipeline, material and mesh merge into one instanced draw**, in `DepthPrepass`, `Opaque` and every shadow pass. The sort key already orders them together. Transparent items stay one draw each, sorted by depth.
- **The draw record is unchanged.** `Fenrir/Draw.glsl` finds an instance's record at the draw's base instance plus its instance index, written per backend (GL's SPIR-V instance index excludes the base). Skinned items merge too, since each record carries its own palette offset.
- **`RendererStats` counts draws and instances apart**, and `RendererConfig` can turn merging off, for comparison.

## Explicit

- **`InstancedMeshRenderer { mesh, material, instances, castShadows, visible }`** draws an **instance list**: a FenrirAssets asset holding many transforms relative to the entity, each with an optional colour, read in shaders through `Fenrir/Draw.glsl`. There is no entity per copy.
- **An asset, not a vector in the component**, so the renderer can tell when it changed: a static field uploads once, and an edit bumps the asset's revision. A component vector would upload every frame.
- **No per-instance culling yet.** Revisit with GPU culling, through indirect draws, when reading the scene or drawing off-screen copies shows up in a frame.
