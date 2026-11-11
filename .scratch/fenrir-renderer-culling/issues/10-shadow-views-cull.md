# 10: Each shadow view culls its own casters, at the level the camera sees them

**What to build:** The shadow passes stop drawing every caster: each cascade, shadowed spot light and shadowed point light culls on the GPU as a camera does (spec: "Culling", "Prerequisite"; ADR-0057, ADR-0050).

- **Shadow candidates** are the opaque and masked runs whose renderer casts (`castShadows`), written once a frame beside the camera candidates, with their own templates and group table.
- **Views:** each cascade culls against its orthographic box's four side planes and its far plane, but not its near plane, so casters between the light and the cascade still cast (drawn with depth clamping or a pulled-back near plane, whichever lighting 15 chose). A spot light culls against its cone's frustum; a point light against its range sphere, once for all six faces, which then draw from the one result.
- **Levels** are chosen by the camera the shadows are for (its position and projection, ticket 07's formula), not the light; a cascade belongs to one camera, and an atlas slot of a spot or point light, shared by every camera, chooses by the first camera that draws it this frame. A copy the camera cannot see still gets a level from its distance to the camera.
- **The shadow passes draw indirectly** from their view's buffers, as `DepthPrepass` does.
- **Coverage:** all three view kinds, built once after lighting 15, 17 and 18 have landed. The README says how a later shadow pass adds its views.

**Blocked by:** 07 (and the spec's Prerequisite: lighting fifteen, seventeen and eighteen merged)

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`

**Status:** resolved

- [x] CPU unit tests: a cascade's cull planes keep a caster between the light and the cascade and drop one beside it; a point light's sphere test.
- [x] `RendererProbe` on null: one cull dispatch per cascade and per shadowed spot and point light; a renderer with `castShadows` off is in no shadow view's templates; a point light's six faces draw from one command buffer.
- [x] GL readback: for a seeded scene, each cascade's counts equal the CPU oracle's, and each survivor's level is the camera's choice.
- [x] GL pixel probes and golden images: a caster outside the camera's view but inside a cascade still shadows the visible ground; ticket 15's shadow goldens are unchanged.

Hand check: the asteroid ring's frame with the planet's shadow costs less than it did unculled, and rocks show no shadow acne from mismatched levels.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

- A cascade's cull volume is its orthographic box's planes with the near one left out (`Core/ShadowViews.hpp`'s `CascadeCullVolume`), which needed `Math::Frustum::FromPlanes` in FenrirMath, beyond the Touches line: a frustum made from planes already normalised, so a volume can be left open on one side. A point light culls by its range sphere alone, a new `vec4 sphere` in the job block (256 bytes now) tested after the planes as `WithinRange` tests it on the CPU: the candidate's sphere, then the world box about its moved corners.
- A point light's instanced faces draw six instances a kept candidate: the level table entry gained `instances` (five words), the pass adds that many to the command's count and writes one indirection entry for all six, which `Fenrir/Draw.glsl`'s POINT_SHADOW path reads at the instance index over six. The faced runs (shaders without the variant, or a device without vertex-stage layers) follow the instanced runs in the same cull, their commands after, so one dispatch culls a light and every face draws from the one buffer.
- The cameras' culls (frustum and LodView) are now found before the shadow passes, since a spot or point light's map chooses levels by the first camera drawn this frame; a cascade by its own camera, frozen with it. Shadow views never freeze: freezing is for seeing a camera's cull.
- Shadow views are not read back into `RendererStats::instancesDrawn`, which stays the cameras' count; `RendererProbe::ShadowCulls` lists them for the tests with each one's volume, level view and buffers.
- The hand check (the asteroid ring with the planet's shadow against unculled, and no acne from mismatched levels) is left to the developer.
