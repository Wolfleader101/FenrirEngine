# 02: `Math::Frustum` tests boxes, records carry their mesh's box, and entities off screen are not drawn

**What to build:** The CPU half of culling: the maths the cull pass repeats, the bounds it reads, and the culls that stay on the CPU (spec: "Culling", "Testing"; ADR-0057).

- **`Math::Frustum` in FenrirMath**, public, so a game culls its own chunks or cells with the maths the renderer uses (ADR-0057 "Where the line is"): normalised planes from a view-projection matrix under reversed-Z (ADR-0026), five for a perspective view with an infinite far plane, six for an orthographic one; a sphere test; a box test that transforms an AABB's eight corners by a matrix and keeps the box unless every corner is outside one plane (the test the cull shader repeats); and a sphere from a box under a matrix (the transformed centre, the half-diagonal scaled by the largest axis scale), the cheap first test.
- **`DrawItem` gains the mesh's local box** (`vec4 boundsMin, boundsMax`) in `Fenrir/Draw.glsl` and its C++ mirror: for a lone or merged item, its mesh's bounds, or `SkinnedBounds` for a skinned item, with `world` already applied so the shader's transform is the identity; for an instance list's record, the mesh's local bounds untouched. Nothing reads it yet but the tests.
- **An instance list's world bounds**: the union of its copies' boxes, computed in local space when the list is uploaded and moved by the entity's world matrix; `Renderer::GetWorldBounds` returns it for an `InstancedMeshRenderer` entity, as it does a `MeshRenderer`'s.
- **Transparent items outside a camera's frustum are not drawn**: the box test on the CPU before ADR-0019's back-to-front sort, per camera. `RendererStats::instances` counts only those drawn.
- **`Renderer::GetFrustum(camera)`** (and the static form): the camera's `Math::Frustum` this frame, for a game that culls or streams its own entities by it. The API description gains it (generated output only).

**Blocked by:** none

**Touches:** `packages/FenrirMath/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/include/Fenrir/Draw.glsl`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

- [ ] FenrirMath unit tests: planes of a known perspective and orthographic view; spheres and boxes inside, outside and straddling each plane; a long thin box the sphere keeps and the box test drops; a box under a non-uniform scale and a mirror; the derived sphere encloses every transformed corner.
- [ ] `RendererProbe` on null: a transparent item behind the camera records no draw, one straddling the edge does; opaque items are all still drawn; a record's bounds bytes hold the expected box; an instance list's `GetWorldBounds` encloses every copy under a moved entity; `GetFrustum` matches the unit tests' planes for the camera.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
