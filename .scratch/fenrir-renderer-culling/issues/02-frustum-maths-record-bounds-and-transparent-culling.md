# 02: Views have frustum planes, records carry bounding spheres, and transparent items off screen are not drawn

**What to build:** The CPU half of culling: the maths the cull pass repeats, the bounds it reads, and the one cull that stays on the CPU (spec: "Culling", "Testing"; ADR-0055).

- **`Math::Frustum` in FenrirMath**, public, so a game culls its own chunks or cells with the maths the renderer uses (ADR-0055 "Where the line is"): normalised planes from a view-projection matrix under reversed-Z (ADR-0026), five for a perspective view with an infinite far plane, six for an orthographic one; a sphere test; a sphere from an AABB (centre and half-diagonal); a sphere moved by a matrix, its radius scaled by the largest axis scale.
- **`DrawItem` gains `vec4 bounds`** in `Fenrir/Draw.glsl` and its C++ mirror: a lone or merged item's world sphere, from its mesh's bounds under its world matrix, or from `SkinnedBounds` for a skinned item; an instance list's record holds its mesh's local sphere. Nothing reads it yet but the tests.
- **An instance list's world bounds**: the union of its copies' spheres under the entity's world matrix, from a local union computed when the list is uploaded; `Renderer::GetWorldBounds` returns it for an `InstancedMeshRenderer` entity, as it does a `MeshRenderer`'s.
- **Transparent items outside a camera's frustum are not drawn**: tested on the CPU by their world sphere before ADR-0019's back-to-front sort, per camera. `RendererStats::instances` counts only those drawn.
- **`Renderer::GetFrustum(camera)`** (and the static form): the camera's `Math::Frustum` this frame, for a game that culls or streams its own entities by it.

**Blocked by:** none

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/include/Fenrir/Draw.glsl`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/api/`, `packages/FenrirMath/`

**Status:** ready-for-agent

- [ ] FenrirMath unit tests: planes of a known perspective and orthographic view; spheres inside, outside and straddling each plane; a sphere under a non-uniform scale and a mirror is no smaller than the moved AABB's.
- [ ] `RendererProbe` on null: a transparent item behind the camera records no draw, one straddling the edge does; opaque items are all still drawn; a record's bounds bytes hold the expected sphere; an instance list's `GetWorldBounds` encloses every copy under a moved entity; `GetFrustum` matches the unit tests' planes for the camera.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
