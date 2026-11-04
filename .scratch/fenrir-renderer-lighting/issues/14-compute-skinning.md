# 14: Skinned meshes are skinned once a frame in compute

**What to build:** Every later pass draws skinned meshes from per-frame skinned vertex buffers (spec: "Shadows"; ADR-0050, amending ADR-0035).

- **A compute pass** at the start of the frame skins each drawn skinned mesh once into a per-frame vertex buffer (position, normal, tangent), ring-buffered per frame in flight, from ADR-0035's palette buffer.
- **Every pass draws those buffers rigidly**; deduplication, skip cases and bounds are unchanged.
- **`SKINNED` stays** for user shaders without a compute path and as the fallback when `RendererConfig` turns compute skinning off.

**Blocked by:** 10

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`

**Status:** resolved

- [x] GL pixel probes: compute-skinned and vertex-skinned results match for ADR-0035's test meshes.
- [x] `RendererProbe` on null: one skinning dispatch per skinned mesh per frame however many passes draw it.

Hand check: AnimationDemo looks the same with compute skinning on and off.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-11-01, stacked on ticket 13's branch (#265 to #272 not yet merged). Decisions to review:

- **The device's first compute work.** `ComputePipelineDesc` and `GraphicsDevice::CreateComputePipeline` (null and GL),
  compute passes (a `PassDesc` with `QueueKind::Compute` and no targets), and `ResourceUsage::VertexInput` with
  `PassBuilder::VertexInput`. The GL backend links compute programs through the same program cache, dispatches, and
  puts `glMemoryBarrier(VERTEX_ATTRIB_ARRAY | SHADER_STORAGE)` at the end of a compute pass that declares a storage
  write. `ShaderStage::Compute` joins the container format (no version bump: old containers still read), and
  FenrirShaderCompiler and `fenrir_add_shaders` take `--compute` / `COMPUTE` in place of the vertex and fragment stages.
  These sit outside the ticket's "Touches" list (Tools/, FenrirAssets, cmake), but nothing compute could run without.
- **The pass.** `Skinning`, in a new `RenderStage::Skinning` between `Prepare` and `Shadow`, runs `builtin:skinning`
  (64 invocations a group, one a vertex) once per (mesh, palette offset) the frame draws. It reads the mesh's vertex
  buffer as words (a skinned mesh's buffer gains storage usage and a set 1 group), the palette, and a 48-byte job block
  per dispatch (stream offsets in words, from a uniform `BlockRing`), and writes three streams a mesh (positions,
  normals, tangents; 40 bytes a vertex, 16-byte aligned) into a skinned vertex buffer, a `FrameRingBuffer` grown by
  doubling. DepthPrepass, Opaque and Transparent declare it as vertex input and bind slots 0 to 2 from it.
- **Which shaders, read from the ticket's "`SKINNED` stays for user shaders":** only the built-in mesh programs
  (`builtin:lit`, `builtin:unlit`, the fallback) are compute-skinned, told by comparing the prepared shader's bytes with
  the embedded containers. A user shader's skinned draws keep its own `SKINNED` variant, which may do more than skin;
  a user shader without the key falls back to the fallback material as before, which is compute-skinned.
- **Instancing:** compute-skinned items merge only with items drawing the same skinned vertices (one mesh on one
  skeleton). AnimationDemo's crowd of 64 is 65 draws with compute skinning on and 2 with it off. ADR-0051 is amended.
- **`Renderer::SetComputeSkinning(bool)`** (and the `App&` form), beside `RendererConfig::computeSkinning`, for the
  hand check: AnimationDemo's Crowd scene toggles it with C and shows it on screen. It is the renderer's, so it stays as
  left when the scene changes.
- **The maths.** `Fenrir/SkinMaths.glsl` holds the blend, the cofactor normal with its fallbacks and the tangent's
  re-orthogonalisation; `Fenrir/Skinning.glsl` (the vertex path) and `skinning.comp` both use it. The compute path makes
  the tangent perpendicular to the normal in model space, before the skeleton entity's world matrix, so under a
  non-uniformly scaled skeleton entity it can differ slightly from the vertex path's world-space Gram-Schmidt, as any
  rigid mesh's tangent frame does there.
- **Tests:** `Tests/ComputeSkinning.cpp` (null: one dispatch per mesh and palette range across two views and three
  passes, 3 dispatches for 10 skinned draws; draws read slots 0 to 2 from the skinned buffer where their job writes;
  job blocks, capacity and sources; merging on and off; turning it off and on; the config; skipped and hidden meshes add
  no pass; the fallback for a shader without `SKINNED`; a user shader's `SKINNED` variant; the program's reflection).
  `Tests/OpenGL/Skinning.cpp` holds compute and vertex skinning to each other pixel by pixel over ADR-0035's four
  scenes and a normal-mapped one (flipping the compute tangent's handedness fails it). Four older tests of the vertex
  path now turn compute skinning off; the GL merged-skinning test runs both ways. Shader compiler and container tests
  cover the compute stage.
- **msvc-x64-debug:** the whole CTest tree passed (GL included, RTX 5080), but for one PhysicsDemo smoke run that timed
  out under `-j 6` and passed on its own; after the review fixes, the renderer, assets, shader compiler and demo suites
  again.

Hand check, done on the RTX 5080: AnimationDemo's Crowd, paused, looks the same with compute skinning on and off
(`screenshots/14-compute-skinning/crowd-compute-on.png`, `crowd-compute-off.png`); only the draw count differs.
