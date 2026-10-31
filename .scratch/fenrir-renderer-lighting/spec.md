Status: ready-for-agent

# FenrirRenderer lighting: physically based shading with image-based lighting, many lights, shadows, ambient occlusion, bloom, tonemapping and instancing, with LightingDemo

Origin: the FenrirRenderer wayfinder map (`.scratch/fenrir-renderer/map.md`), its "Lighting and shadows", "Post-processing", "Golden-image tests" and "Richer materials" lines, settled in a grilling session on 2026-10-31. The engine developer is working through LearnOpenGL's Advanced Lighting and PBR sections, but as a guide, not a spec: where a current technique beats the tutorial's, this spec takes it. It builds on the foundation (`git show 28d8f69:.scratch/fenrir-renderer-foundation/spec.md`) and the animation spec (`git show 592bb8d9^:.scratch/fenrir-renderer-animation/spec.md`), and cites rather than restates them.

| ADR | Decision |
|---|---|
| 0047 | The sky is the scene's ambient light, and is filtered on the GPU into image-based lighting |
| 0048 | Materials light through one shared lighting include over a grown frame set |
| 0049 | The frame stays forward, with clustered lights, a depth prepass, GTAO, bloom and a tonemapped output |
| 0050 | Shadows are stable cascades and pooled atlases with a budget, and skinned meshes skin once per frame in compute |
| 0051 | The renderer instances matching items automatically, and `InstancedMeshRenderer` draws instance lists |
| 0052 | Large demo assets are downloaded at configure time, and tests never use them |

They amend ADR-0019, ADR-0023, ADR-0024, ADR-0026 and ADR-0035.

## Prerequisite

The history rewrite of ADR-0052 runs first, as its own task on `main`: the backpack is replaced by Khronos's `BoxTextured` in RendererDemo, PhysicsDemo, ScriptingDemo and `SpawnModel`'s test, and the large binaries are removed from history. The integration branch `fenrir-renderer-lighting` is cut after it.

## Problem Statement

The foundation's `builtin:lit` is already metallic-roughness GGX into linear HDR, with reversed-Z depth, sRGB output and derivative-frame normal maps. But a scene is lit by one directional light and a flat ambient colour, nothing casts a shadow, the HDR colour is clamped rather than tonemapped, the sky lights nothing, and each mesh is one draw. The engine developer wants:
- **Physically based lighting that looks like it**: the sky lighting the scene through image-based lighting, from HDR panoramas.
- **Many lights**: point, spot and rect lights, any number.
- **Shadows** from the sun, spot and point lights, stable and soft.
- **Ambient occlusion, bloom and a real tonemap** with exposure.
- **Instancing**, automatic and explicit, for tens or hundreds of thousands of copies.
- **User shaders with the same lighting** as the built-in material.
- **Showcase demos to fly around in**, without bloating the repository.

## Solution

All in FenrirRenderer, with an instance-list asset in FenrirAssets and a new LightingDemo app.

### Lights and the sky

- **Lights** (ADR-0048): `DirectionalLight` (one, the sun, gaining `castShadows`), and any number of `PointLight`, `SpotLight` and `RectLight`. Inverse-square falloff windowed at `range`; spot cones by inner and outer angle; rect lights shaded with LTC, one- or two-sided, unshadowed.
- **The sky** (ADR-0047): `ColourSky`, `CubemapSky` and `PanoramaSky`, the last two with `intensity` and `rotation`. `AmbientLight` is removed. Panoramas convert to cubemaps on the GPU; the sky is filtered into L2 spherical harmonics for diffuse and a prefiltered cubemap for specular, cached by revision; a BRDF lookup table is made at start-up. A camera's `ColourBackground` changes the backdrop only.

### Materials

- **`Fenrir/Lighting.glsl`** (ADR-0048): `FenrirSurface` and one lighting function over every light, shadow, the sky and ambient occlusion. `builtin:lit` calls it; user shaders may.
- **`builtin:lit` gains** `occlusionTexture`, `emissiveStrength`, height-correlated Smith visibility, multi-scatter energy compensation and specular anti-aliasing.
- **Tangents:** the importer keeps a mesh's tangents as an optional stream; `lit` uses them when present and the derivative frame otherwise.

### The frame

- **Stages** (ADR-0049): `Shadow, DepthPrepass, Opaque, Background, Transparent, Debug, PostProcess, Output, Overlay`.
- **`DepthPrepass`** writes depth and view-space normals for opaque and masked items; `Opaque` tests for equality.
- **GTAO** from the prepass, per camera, darkening indirect light only.
- **Clustered lights:** a compute pass per view; a plain loop over the light buffer until it lands.
- **Bloom** in `PostProcess`, energy preserving, no threshold.
- **`Output`** applies `exposure` (EV stops) and the `tonemapper` (`None`, `AgX` default, `PbrNeutral`, `Aces`).
- **`Camera` gains** `exposure`, `tonemapper`, `ambientOcclusion { enabled, radius, intensity }` and `bloom { intensity, radius }`.

### Shadows

ADR-0050: `castShadows` on lights and on both mesh renderers; a `RendererConfig` budget of shadowed point (4) and spot (8) lights, chosen by distance each frame; stable texel-snapped cascades for the sun (up to four, practical split scheme, blended boundaries); a 2D atlas for spot shadows and a cubemap array for point shadows, drawn without geometry shaders; hardware comparison with rotated Vogel-disk PCF, slope-scaled and normal-offset biases. Compute skinning skins each skinned mesh once a frame for every pass.

### Instancing

ADR-0051: consecutive opaque, masked and shadow items with the same pipeline, material and mesh merge into instanced draws, which `RendererConfig` can turn off; `RendererStats` counts draws and instances. `InstancedMeshRenderer { mesh, material, instances, castShadows, visible }` draws an instance list asset of transforms and optional colours, uploaded when its revision changes.

### Testing

Each ticket's tests follow the foundation's seams: the null backend through `RendererProbe` for what is drawn and in what order, GL pixel probes where a result is exact, CPU unit tests for maths (cascade fitting, SH projection, falloff, LTC lookup). Ticket 01 adds **golden-image tests** (ADR-0049) on the real GL device: whole frames against committed small PNGs under a perceptual tolerance, regenerated only with an explicit flag. Every demo scene has a smoke run that passes on null without fetched assets.

## LightingDemo

`examples/LightingDemo/` through `fenrir_add_demo_app`, on the scaffold's free-fly camera, with an ImGui panel for exposure, the tonemapper, bloom, ambient occlusion, shadow debug views (cascade colours, atlas view) and the sky. Its assets: committed ones in `examples/LightingDemo/assets/` with an `ATTRIBUTION.md`, the rest through `FENRIR_FETCH_DEMO_ASSETS` (ADR-0052), each file and licence checked before it is added to the manifest. A scene missing fetched assets says so on screen.

1. **Material grid:** spheres varying roughness and metallic under a committed ~2 MB Poly Haven panorama (ticket 07).
2. **Showcase models:** Khronos DamagedHelmet, FlightHelmet and SciFiHelmet under switchable panoramas (07).
3. **Many lights:** a room of moving point and spot lights over PBR surfaces (08), shadowed once 17 and 18 land.
4. **Showroom:** a dark room, a polished floor of varying roughness, a neon strip, a panel and a two-sided sign as rect lights, the helmets on plinths (09).
5. **Asteroid ring:** a planet (Solar System Scope textures, CC-BY 4.0) with a ring of 100k Poly Haven rocks, 3–4 kinds, one `InstancedMeshRenderer` each, under a procedural starfield panorama made by the demo; a field of about 5k rock entities merged automatically; an emissive station with point lights; the panel shows the asteroid count, the merging switch and draw and instance counts (13), shadowed by the sun from 15.
6. **Forest clearing at golden hour:** Poly Haven CC0 trees, ferns, shrubs, rocks and logs as about 20k instances; about 200k procedural grass blades through a user wind shader that lights through `Fenrir/Lighting.glsl`; a Poly Haven ground set and sunset panorama; cascaded shadows, alpha-tested leaves in the prepass and shadows (16).
7. **Neon city at night:** procedural instanced buildings drawn by a user facade shader whose per-instance colour lights windows; asphalt with a puddle mask; rect-light signs reflected by the wet streets through LTC specular; point-light street lamps; spot-light car headlights; a Poly Haven night panorama; bloom, GTAO and AgX (20).
8. **Sponza:** Khronos Sponza under an HDR sky with the sun's cascades, shadowed point lights, GTAO and bloom (21).

## Tickets

| # | Ticket | Blocked by |
|---|---|---|
| 01 | Tonemapping, exposure and golden-image tests | — |
| 02 | Tangent stream | — |
| 03 | `PanoramaSky` | 01 |
| 04 | The lighting include and image-based lighting; `AmbientLight` removed | 03 |
| 05 | BRDF upgrades, `occlusionTexture` and `emissiveStrength` | 02, 04 |
| 06 | Bloom | 01 |
| 07 | LightingDemo, demo-asset fetching, the material grid and the showcase models | 05, 06 |
| 08 | Point and spot lights, and the many-lights scene | 07 |
| 09 | Rect lights and the showroom | 08 |
| 10 | Depth prepass | 05 |
| 11 | GTAO | 10 |
| 12 | Automatic instancing | 10 |
| 13 | `InstancedMeshRenderer`, instance lists and the asteroid ring | 07, 12 |
| 14 | Compute skinning | 10 |
| 15 | Cascaded shadows for the sun | 12, 14 |
| 16 | The forest clearing | 11, 13, 15 |
| 17 | Spot shadows | 08, 15 |
| 18 | Point shadows | 17 |
| 19 | Clustered lights | 09 |
| 20 | The neon city | 06, 11, 13, 19 |
| 21 | Sponza | 11, 18, 19 |
| 22 | C# for lighting, the sky, cameras, shadows and instancing | 21 |

Every C++ ticket touches `packages/FenrirRenderer/src/` and its shaders, so most of them stack. C# waits for every C++ ticket, as in the animation spec; until ticket 22, `AmbientLight`'s C# mirror is a deprecated no-op.

## Out of scope

Deferred shading; physical light units; auto-exposure; screen-space reflections; fog and volumetric light; PCSS and VSM; area-light shadows and disc, tube or sphere lights; clearcoat, sheen, transmission and anisotropy; GPU culling and indirect draws; LearnOpenGL's Blinn-Phong (the engine is already GGX).
