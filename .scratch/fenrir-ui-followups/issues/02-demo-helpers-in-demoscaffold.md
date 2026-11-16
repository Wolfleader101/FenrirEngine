# 02: YawPitch and the screen quad move to DemoScaffold

**What to build:** the demos' copied helpers in DemoScaffold, used from every demo that had a copy (spec: "Cleanups"). A refactor: every demo draws as before and every test passes.

- **`YawPitch(yaw, pitch)`:** the rotation of a yaw about +Y then a pitch about the turned X, in radians, documented with its sign convention. The copies in `examples/UIDemo/src/Room.cpp`, `examples/RendererDemo/src/CamerasAndTargets.cpp` and `examples/RendererDemo/src/MeshesAndMaterials.cpp` are compared first (they must agree; if they do not, say so in the PR and keep each demo's look).
- **A screen-quad mesh builder:** `MeshData` of a width, a height and a texture repeat, centred, facing +Z, with normals and tangents, from UIDemo's `ScreenQuad` and RendererDemo's `Quad`.
- Both in `FenrirDemoScaffold/DemoScaffold.hpp` (or a header of their own beside it if the scaffold's README says helpers go there), with Doxygen blocks, and the scaffold's README lists them.
- **Tests:** DemoScaffold's tests gain one each: `YawPitch` turns -Z to the worked directions for a quarter turn of yaw and of pitch; the quad has four vertices at ±width/2, ±height/2, its normal +Z and its UVs 0 to `repeat`.

**Blocked by:** None - can start immediately

**Touches:** `examples/DemoScaffold/`, `examples/UIDemo/src/Room.cpp`, `examples/RendererDemo/src/CamerasAndTargets.cpp`, `examples/RendererDemo/src/MeshesAndMaterials.cpp`

**Status:** ready-for-agent

- [ ] No demo defines `YawPitch` or its own quad builder.
- [ ] DemoScaffold's new tests pass; UIDemo's and RendererDemo's tests and smoke runs pass.
- [ ] Hand check: UIDemo's room and screen and RendererDemo's cameras and meshes scenes look as before.
