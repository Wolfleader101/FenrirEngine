# What other packages will need from a renderer

Standing notes, not a spec: what the rest of the engine has found it needs from a real renderer, collected so a renderer effort can start from them. Add to it whenever another effort finds a need.

## Where rendering is today

- `packages/FenrirRenderer` is an empty placeholder (its `CMakeLists.txt` has no target).
- All drawing lives in the DemoApp (`examples/DemoApp/src/GLRenderer.cpp`, `ModelLibrary.cpp`): OpenGL, meshes loaded with assimp into static buffers (`GL_STATIC_DRAW`), a skybox, and one `GL_LINES` pass for physics debug lines.
- No package depends on a renderer, and physics depends on none ("Packages stay independent" in `.scratch/fenrir-physics/map.md`). Whatever a renderer offers, physics should keep producing plain data that a renderer reads, as its debug lines do.

## Needs found so far

### Debug lines (FenrirPhysics, in use)

- FenrirPhysics produces `DebugLine { from, to, colour }` spans per world, regenerated after each step; the DemoApp uploads and draws them every frame in one `GL_LINES` pass.
- A renderer should take a span of coloured lines (and later perhaps points) per frame, drawn over the scene, without the caller owning GL buffers.
- Settled by ADR-0025: physics draws into FenrirDebugDraw instead of handing out a span.

### Live text (DemoApp, worked around)

- There is no text overlay. The demo scenes show live values (joint angles and loads, a bob phase) by writing them into an entity's `Name`, read in the editor's Scene Hierarchy.
- A renderer, or a debug UI layer above it, should draw short text in screen space and at world positions, from C++ and C#.
- Settled by ADR-0025: screen text, keyed on-screen messages and world text through FenrirDebugDraw.

### Ragdolls (FenrirPhysics, waiting)

- A **skeleton**: a hierarchy of bones with bind poses, which the engine does not have; physics needs a mapping from bones to bodies and joints.
- An **animation system** to blend between an animated pose and the ragdoll's simulated pose (going limp, powered ragdolls driving joint motors towards the animation).
- **Skinned-mesh rendering**: vertices weighted to bones, drawn from the current bone transforms each frame (typically on the GPU from a bone-matrix buffer).
- Physics would write bone transforms as plain data; the renderer and animation system read them.

### Soft bodies (FenrirPhysics, waiting)

- **Meshes whose vertices change every frame**: the simulation moves every vertex each step, so the renderer must accept new vertex positions per frame (dynamic or streamed buffers, not `GL_STATIC_DRAW`), ideally without the caller rebuilding the whole mesh.
- **Normals recomputed** from the moved vertices (on the CPU from physics's data, or in a shader), and a stable index buffer and UVs.
- Physics would expose each soft body's vertex positions as a plain-data span after each step, as it does for debug lines.
