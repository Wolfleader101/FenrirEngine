Status: ready-for-agent

# FenrirRenderer culling: per-view GPU culling of opaque draws into indirect draws, and mesh levels of detail

Origin: lighting ticket 13 (`.scratch/fenrir-renderer-lighting/issues/13-instanced-mesh-renderer-and-asteroid-ring.md`) measured LightingDemo's asteroid ring, and the engine developer asked what else would make it faster. The lighting spec put GPU culling and indirect draws out of scope; this spec takes them, with levels of detail. It builds on the lighting spec (`.scratch/fenrir-renderer-lighting/spec.md`) and cites rather than restates it.

| ADR | Decision |
|---|---|
| 0055 | Opaque draws are culled per view on the GPU into indirect draws, and meshes carry LODs chosen by projected error |

It amends ADR-0051 ("No per-instance culling yet") and ADR-0021.

## Prerequisite

Lighting tickets 13 (`InstancedMeshRenderer` and instance lists) and 14 (compute skinning, which brings compute pipelines, dispatch and storage-buffer barriers to the GL backend) are merged into `fenrir-renderer-lighting`. The integration branch `fenrir-renderer-culling` is cut from it then. Ticket 05 touches only FenrirAssets and needs neither.

The shadow tickets (lighting 15, 17 and 18) may land before or after this spec's tickets: ticket 09 culls every shadow view that exists when it is built, and a shadow ticket built after it gives its views a cull the same way (ADR-0055).

## Problem Statement

In Release on an RTX 5080 at 1280x720 with 4x MSAA, the asteroid ring's 100k full-detail Poly Haven rocks took 136 ms a frame with the GPU fully busy and the CPU at 0.04 cores. Nothing is culled: every copy, on screen or not, is drawn in `DepthPrepass` and `Opaque`, and each shadow pass will draw them all again. Nothing has levels of detail: a rock a few pixels across is drawn with thousands of triangles, shading micro-triangles. The demo hides this by simplifying its meshes by vertex clustering at spawn (about 9 ms), which loses detail up close and is not something every app should write.

## Solution

All in FenrirRenderer, with levels of detail in FenrirAssets (ADR-0055).

### Culling

- **A cull pass per view**, one compute dispatch over every opaque and masked candidate: each lone item, each merged run's items, each instance list's copies. A candidate survives if its bounding sphere is inside the view's frustum planes (five under the camera's infinite far plane, six for an orthographic or shadow view).
- **Views:** each camera, whose result `DepthPrepass` and `Opaque` share; each cascade, shadowed spot light and shadowed point light (by its range sphere) once shadows exist.
- **Bounds:** each record of the draw buffer gains a sphere, world-space for items, mesh-local for instance lists.
- **Indirection:** set 3 binding 3, one entry per drawn instance naming its record and copy; `CurrentDraw()` reads it.
- **Indirect draws:** per run and level of detail, a command template copied over the view's command buffer before the cull and counted into with atomics; each run draws as one multi-draw.
- **Transparent items** are culled on the CPU by their world bounds and stay direct draws.
- **`RendererConfig::gpuCulling`** (on) turns culling off for comparison; **`Renderer::SetCullingFrozen`** keeps culling against the camera as it was, a debug view for flying around the result.
- **`RendererStats`** keeps `instances` as candidates submitted, and gains `instancesDrawn` and `drawnByLod`, read back from the GPU once their frame has left flight.

### Levels of detail

- **`MeshData::lods`**: extra index lists over the mesh's own streams, each with its simplification error in local units, coarser each; the store checks them.
- **`GenerateLods(const MeshData&, const LodSettings&)`** in FenrirAssets, through meshoptimizer (MIT, a submodule under `packages/FenrirAssets/libs/`), at load. No cooked asset format and no build-time tool.
- **Selection** in the cull pass: the coarsest level whose error projects below `RendererConfig::lodErrorPixels` (1.0) at the copy's distance from the camera; shadow views select by the camera.

### Occlusion (last, optional)

Two-phase Hi-Z occlusion from the depth prepass (ADR-0055 "Not yet"), as ticket 10. Build it only if a scene shows hidden copies costing a frame once 01 to 09 have landed.

### Testing

The foundation's seams, as the lighting spec's:
- **CPU unit tests** for the maths the GPU repeats: frustum planes from a view-projection, sphere tests, a copy's world sphere, level selection. They are the oracle for the GL tests.
- **The null backend through `RendererProbe`** for what is recorded: which dispatches run per view, which templates and group tables are written, which indirect draws each pass records, and their declared uses. The null backend runs no compute, so it never knows survivors.
- **GL tests** read the indirect command buffers back (through `GlProbe::GlName` and the GL context) and compare each run's per-level instance counts with the CPU oracle over the same scene, exactly; **pixel probes** show copies at the frustum's edges and levels of distinct shape.
- LightingDemo's scenes keep their null smoke runs.

## Tickets

| # | Ticket | Blocked by |
|---|---|---|
| 01 | Indirect draws in the graphics device | — |
| 02 | Frustum maths, record bounds and CPU culling of transparent items | — |
| 03 | GPU culling for cameras: the cull pass, the indirection and indirect runs | 01, 02 |
| 04 | `RendererStats` count what the GPU drew, and culling can be frozen | 03 |
| 05 | Mesh levels of detail in FenrirAssets, generated by meshoptimizer | — |
| 06 | The cull pass selects a level of detail per copy | 03, 05 |
| 07 | The asteroid ring on generated levels, with culling and level counts in the panel | 04, 06 |
| 08 | C# for culling, statistics and levels of detail | 07 |
| 09 | Shadow views cull, choosing levels by the camera | 06 |
| 10 | Two-phase Hi-Z occlusion culling | 07 |

Tickets 01 to 04, 06, 09 and 10 touch `packages/FenrirRenderer/src/` and its shaders, so they stack: 01, 02, 03, 04, 06, then 07 for the demo, with 09 wherever the lighting stack's shadows are when it starts, and 10 last. Ticket 05 touches only FenrirAssets and runs beside 01 to 04 off the base. Ticket 09 needs lighting 15 merged into the base, and covers lighting 17 and 18 if they have landed.

## Out of scope

Meshlets, cluster culling and continuous LOD; mesh shaders; GPU-written draw lists of whole items; LOD cross-fading or dithered transitions; LODs for transparent items; a cooked asset format or a build-time LOD tool; glTF's `MSFT_lod` extension; per-submesh levels (the importer already makes one mesh per submesh).
