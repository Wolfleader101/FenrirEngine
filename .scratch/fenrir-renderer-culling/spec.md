Status: ready-for-agent

# FenrirRenderer culling: per-view GPU culling of opaque draws into indirect draws, occlusion, and mesh levels of detail

Origin: lighting ticket 13 (`.scratch/fenrir-renderer-lighting/issues/13-instanced-mesh-renderer-and-asteroid-ring.md`) measured LightingDemo's asteroid ring, and the engine developer asked what else would make it faster. The lighting spec put GPU culling and indirect draws out of scope; this spec takes them, with occlusion and levels of detail. Settled in a grilling session on 2026-11-03. It builds on the lighting spec (`.scratch/fenrir-renderer-lighting/spec.md`) and cites rather than restates it.

| ADR | Decision |
|---|---|
| 0057 | Opaque draws are culled per view on the GPU into indirect draws, and meshes carry LODs chosen by projected error |

It amends ADR-0051 ("No per-instance culling yet"), ADR-0021 and ADR-0049, and shares a depth pyramid with ADR-0056 (screen-space reflections).

## Prerequisite

Lighting tickets 13 (`InstancedMeshRenderer` and instance lists) and 14 (compute skinning, which brings compute pipelines, dispatch and storage-buffer barriers to the GL backend) are merged into `fenrir-renderer-lighting`. The integration branch `fenrir-renderer-culling` is cut from it then. Ticket 06 touches only FenrirAssets and needs neither.

Ticket 10 waits for all three shadow tickets (lighting 15, 17 and 18), so it is built once for every view kind. Ticket 11 shares its pyramid with lighting 29: whichever lands first builds the passes, the other extends them.

## Problem Statement

In Release on an RTX 5080 at 1280x720 with 4x MSAA, the asteroid ring's 100k full-detail Poly Haven rocks took 136 ms a frame with the GPU fully busy and the CPU at 0.04 cores. Nothing is culled: every copy, on screen or not, is drawn in `DepthPrepass` and `Opaque`, and each shadow pass will draw them all again. Nothing has levels of detail: a rock a few pixels across is drawn with thousands of triangles, shading micro-triangles. The demo hides this by simplifying its meshes by vertex clustering at spawn (about 9 ms), which loses detail up close and is not something every app should write.

## Solution

All in FenrirRenderer, with levels of detail in FenrirAssets and the frustum maths in FenrirMath (ADR-0057).

### Culling

- **Two levels** (ADR-0057 "Where the line is"): the CPU culls entities by their world bounds (an instance list's is the union of its copies'), so a run, item or list outside a view is not a candidate for it; the GPU culls copies over a flat list. The renderer keeps no spatial structure, and a game that wants one over its chunks writes it over `Renderer::GetWorldBounds` and the public `Math::Frustum` (`Renderer::GetFrustum`).
- **A cull pass per view**, one compute dispatch over every opaque and masked candidate: each lone item, each merged run's items, each instance list's copies. A candidate survives two tests against the view's planes (five under the camera's infinite far plane, six for an orthographic or shadow view): the sphere derived from its box, then its box's eight transformed corners.
- **Views:** each camera, whose result `DepthPrepass` and `Opaque` share; each cascade, shadowed spot light and shadowed point light (by its range sphere). The passes run in a new `Cull` stage, first, open to user passes.
- **Bounds:** each record of the draw buffer carries its mesh's local box, with the world matrix already applied for an item and untouched for an instance list.
- **Indirection:** set 3 binding 3, one entry per drawn instance naming its record and copy; `CurrentDraw()` reads it.
- **Indirect draws:** per run and level of detail, a command template copied over the view's command buffer before the cull and counted into with atomics, so the order within a run is not fixed and tests compare sets; each run draws as one multi-draw.
- **Transparent items** are culled on the CPU by their world bounds and stay direct draws.
- **`RendererConfig::gpuCulling`** (on) turns culling off for comparison; **`Renderer::SetCullingFrozen`** keeps culling against the camera as it was, a debug view for flying around the result.
- **`RendererStats`** keeps `instances` as candidates submitted, and gains `instancesDrawn` and `drawnByLod`, read back from the most recent frame that has left flight.

### Occlusion

- **Two phases per camera**, on by default (`RendererConfig::occlusionCulling`): last frame's visible candidates draw into the prepass first; a depth pyramid is built from them; the rest are tested against its farthest depth and the survivors draw after. `Opaque` draws both.
- **The pyramid is ADR-0056's**, shared with reflections: per camera, RG32F nearest and farthest depth, fragment passes into mips (no storage images), made when either wants it, and built again from the complete depth for GTAO and reflections.
- **Shadow views do not occlusion cull.**

### Levels of detail

- **`MeshData::lods`**: extra index lists over the mesh's own streams, each with its simplification error in local units, coarser each; the store checks them.
- **`GenerateLods(const MeshData&, const LodSettings&)`** in FenrirAssets, through meshoptimizer (MIT, a submodule under `packages/FenrirAssets/libs/`), at load, called by the app. No cooked asset format and no build-time tool; no per-mesh or per-renderer override.
- **Selection** in the cull pass: the coarsest level whose error projects below `RendererConfig::lodErrorPixels` (1.0) at the copy's distance from the camera; shadow views select by the camera. A copy whose radius projects under `RendererConfig::minProjectedPixels` (0, off) is dropped.

### Testing

The foundation's seams, as the lighting spec's:
- **FenrirMath and CPU unit tests** for the maths the GPU repeats: frustum planes from a view-projection, the sphere and box tests, level selection, the pyramid's levels. They are the oracle for the GL tests.
- **The null backend through `RendererProbe`** for what is recorded: which dispatches run per view, which templates and group tables are written, which indirect draws each pass records, and their declared uses. The null backend runs no compute, so it never knows survivors.
- **GL tests** read the indirect command buffers back (through `GlProbe::GlName` and the GL context) and compare each run's per-level instance counts and the set of surviving copies with the CPU oracle over the same scene, exactly; **pixel probes and golden images** show frames unchanged by the plumbing, copies at the frustum's edges, and levels of distinct shape.
- LightingDemo's scenes keep their null smoke runs.

## Tickets

| # | Ticket | Blocked by |
|---|---|---|
| 01 | Indirect draws in the graphics device | — |
| 02 | `Math::Frustum`, record and instance-list boxes, the coarse entity cull and CPU culling of transparent items | — |
| 03 | The cull pass, the indirection and indirect draws, accepting everything | 01, 02 |
| 04 | The frustum test in the cull pass, and `gpuCulling` | 03 |
| 05 | `RendererStats` count what the GPU drew, and culling can be frozen | 04 |
| 06 | Mesh levels of detail in FenrirAssets, generated by meshoptimizer | — |
| 07 | The cull pass selects a level of detail per copy, and `minProjectedPixels` | 04, 06 |
| 08 | The asteroid ring on generated levels, with culling and level counts in the panel | 05, 07 |
| 09 | C# for culling, statistics and levels of detail | 08 |
| 10 | Shadow views cull, choosing levels by the camera | 07 (+ lighting 15, 17, 18) |
| 11 | Two-phase Hi-Z occlusion culling, on by default | 08 (+ lighting 29 for the pyramid, either order) |

Tickets 01 to 05, 07, 10 and 11 touch `packages/FenrirRenderer/src/` and its shaders, so they stack: 01, 02, 03, 04, 05, 07, then 08 for the demo, 11, and 10 once the shadow tickets are in the base. Ticket 06 touches only FenrirAssets and runs beside 01 to 05 off the base; 02 is the only ticket touching FenrirMath.

## Out of scope

The game's side of ADR-0057's line: world streaming, spatial partitioning (octrees, grids, cells and portals), HLOD proxies and impostors, and potentially visible sets; the engine gives them bounds, the frustum, `visible` and revisions. GPU-written instance lists (a later ADR); meshlets, cluster culling and continuous LOD; mesh shaders; GPU-written draw lists of whole items; LOD cross-fading or dithered transitions; per-mesh or per-renderer LOD overrides; LODs for transparent items; occlusion culling of shadow views; a cooked asset format or a build-time LOD tool; glTF's `MSFT_lod` extension; per-submesh levels (the importer already makes one mesh per submesh).
