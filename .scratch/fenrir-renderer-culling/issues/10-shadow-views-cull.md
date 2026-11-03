# 10: Each shadow view culls its own casters, at the level the camera sees them

**What to build:** The shadow passes stop drawing every caster: each cascade, shadowed spot light and shadowed point light culls on the GPU as a camera does (spec: "Culling", "Prerequisite"; ADR-0057, ADR-0050).

- **Shadow candidates** are the opaque and masked runs whose renderer casts (`castShadows`), written once a frame beside the camera candidates, with their own templates and group table.
- **Views:** each cascade culls against its orthographic box's four side planes and its far plane, but not its near plane, so casters between the light and the cascade still cast (drawn with depth clamping or a pulled-back near plane, whichever lighting 15 chose). A spot light culls against its cone's frustum; a point light against its range sphere, once for all six faces, which then draw from the one result.
- **Levels** are chosen by the camera the shadows are for (its position and projection, ticket 07's formula), not the light; a cascade belongs to one camera, and an atlas slot of a spot or point light, shared by every camera, chooses by the first camera that draws it this frame. A copy the camera cannot see still gets a level from its distance to the camera.
- **The shadow passes draw indirectly** from their view's buffers, as `DepthPrepass` does.
- **Coverage:** all three view kinds, built once after lighting 15, 17 and 18 have landed. The README says how a later shadow pass adds its views.

**Blocked by:** 07 (and the spec's Prerequisite: lighting fifteen, seventeen and eighteen merged)

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`

**Status:** ready-for-agent

- [ ] CPU unit tests: a cascade's cull planes keep a caster between the light and the cascade and drop one beside it; a point light's sphere test.
- [ ] `RendererProbe` on null: one cull dispatch per cascade and per shadowed spot and point light; a renderer with `castShadows` off is in no shadow view's templates; a point light's six faces draw from one command buffer.
- [ ] GL readback: for a seeded scene, each cascade's counts equal the CPU oracle's, and each survivor's level is the camera's choice.
- [ ] GL pixel probes and golden images: a caster outside the camera's view but inside a cascade still shadows the visible ground; ticket 15's shadow goldens are unchanged.

Hand check: the asteroid ring's frame with the planet's shadow costs less than it did unculled, and rocks show no shadow acne from mismatched levels.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
