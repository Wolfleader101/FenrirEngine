# 18: Point lights cast shadows from a cubemap array

**What to build:** Point shadows within a budget, without geometry shaders (spec: "Shadows"; ADR-0050).

- **`PointLight`'s `castShadows`** and **`RendererConfig`'s point budget** (4 by default), filled as for spot lights.
- **A cubemap array** pooled by the budget, far plane at the light's `range`, depth as linear distance.
- **Six faces without a geometry shader:** as instances writing `gl_Layer` from the vertex stage where supported, else one draw per face.
- LightingDemo's many-lights scene gains point shadows.

**Blocked by:** 17

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `examples/LightingDemo/`

**Status:** resolved

- [x] `RendererProbe` on null: each shadowed point light draws its casters into six faces, as instances where supported.
- [x] GL pixel probes: a caster's shadow on each side of a point light.

Hand check: no seams at cube face edges.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

Built 2026-11-02, stacked on ticket 17's branch (#265 to #277 not yet merged). Decisions to review:

- **The device grew three things.** `TextureKind::CubeArray` (with `TextureDesc::layers`, cubes), a depth attachment
  of one layer or of every layer at once (`DepthAttachment::layer` and `layered`; the graph's
  `DepthAttachmentLayer` and `LayeredDepthAttachment`), and `GraphicsDevice::Features()` with
  `DeviceFeatures::vertexLayer`. The GL backend sets it when the driver lists `SPV_EXT_shader_viewport_index_layer`
  among its SPIR-V extensions (it ingests SPIR-V, so the GLSL extension alone is not enough), or NVIDIA's
  `SPV_NV_viewport_array2`, the same capability under another name: NVIDIA's 621 driver lists only that, so the
  backend renames a stage's `OpExtension` to it when loading. It logs the feature at start; the null device reports it
  on, and `RendererProbe::SetFeatures` turns it off.
- **Instanced faces through the item's own vertex stage.** A `POINT_SHADOW` variant key: under it `Fenrir/Draw.glsl`
  reads instance i as item i / 6 and face i % 6, turns the item's world matrix about the light (the view block's camera
  position) into that face's view and writes `gl_Layer` (cube from `shadowInfo.z`). The shared view block looks down
  -Z from the light, unturned. Any vertex stage that calls `CurrentDraw` works unchanged; the built-in mesh programs
  list the key. A shader without it is drawn a face at a time (`PointShadowFaces` passes, one a face), as is
  everything on a device without vertex layers; both kinds mix in one frame (the instanced pass clears, the face
  passes load).
- **Linear distance from the fragment, not the vertex.** builtin:shadow's `POINT_SHADOW` fragment variant rebuilds the
  distance from `gl_FragCoord` and the infinite reversed-Z depth (near over the distance along the face's axis), so it
  needs nothing from the item's vertex stage, and writes 1 - distance / range: reversed like every other depth here,
  cleared to 0, read with the same `GreaterEqual` comparison sampler. The range is the far plane in that sense; the
  rasteriser's projection stays infinite.
- **Choice and order as spot lights.** `ChooseSpotShadows` became `ChooseShadowedLights(…, kind)` (and
  `SpotShadowChoice` `ShadowChoice`), with `ChoosePointShadows` beside it. Shadowed points follow shadowed spots in the
  light buffer; frame block `lights.z` counts them. Set 0 binding 9 holds their records (`GpuPointShadow`, 16 bytes:
  the biases, a texel's size one unit out, the filter radius), binding 10 the cube array; the frame group's
  `unshadowed` copy binds "No point shadow" (one cube of 0) there for the shadow passes.
- **Reading.** `FenrirPointLightShadow`: the sun's 16-tap Vogel disk laid across the way from the light, biases at the
  receiver in the cube's texels where the pixel is. `samplerCubeArrayShadow` has no explicit-LOD lookup in core GLSL,
  so it uses `texture()`; the cubes have one level. Cube arrays filter across face edges (GL's seamless cube maps are
  on), which is why the faces need no margin.
- **Per-light biases.** `PointLight` gains `shadowNormalBias` and `shadowSlopeBias` as `SpotLight` did; ADR-0050 is
  amended for the whole ticket.
- **Not asked for:** `RendererConfig::pointShadowMapSize` (512, so the default 4 cubes are 24 MiB); LightingDemo's J key,
  cast checkbox and 0 to 8 point budget slider. No debug view of the cubes.
- **Not done:** casters are not culled by a light's range (every caster is drawn into every shadowed light's cube);
  no fade as lights enter or leave the budget; no check that a huge budget fits the GPU's layer limit.
- **Tests:** `Tests/PointShadows.cpp` (CPU: each face's turn against `FenrirFaceDirection`'s layout, near and texel
  size, the choice; null: one layered `PointShadows` pass and six instances a light, a pass a face without vertex
  layers, a `POINT_SHADOW`-less user shader mixed in, light order and `lights.z`, bindings 9 and 10, one warning, the
  cubes remade and freed), `Tests/OpenGL/Shadows.cpp` (a box's shadow on a wall behind it on each of the six sides,
  the box off the face's axis so a swapped or mirrored face misses, drawn instanced and a face at a time; fails with
  the shadow multiply in `Lighting.glsl` removed). Set 0 counts in
  Lights, Sky, SkyLighting and Scripting Drawing went 9 to 11, Flattening 17 to 19.
- **msvc-x64-debug:** the whole CTest tree passed (GL included, RTX 5080).

Screenshots: `screenshots/18-point-shadows/` (Many lights with every shadow on, and with point shadows off). Hand
check, by the user: in Many lights, look along a point light's diagonals, where its cube's faces meet, for seams.
