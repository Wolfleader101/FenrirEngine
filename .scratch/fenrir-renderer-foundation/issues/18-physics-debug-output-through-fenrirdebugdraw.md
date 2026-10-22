# 18: FenrirPhysics draws its debug output through FenrirDebugDraw

**What to build:** Physics's debug lines are drawn into each world's scene's `DebugDraw` in a `Physics` group, so one system shows everything, and the packed `DebugLine` output only the DemoApp could draw is gone (spec: "FenrirPhysics: debug output through FenrirDebugDraw"; ADR-0025). This is the physics map's "Debug output through FenrirDebugDraw" line, built here because PhysicsDemo (tickets 28 and 29) needs it.

- **FenrirPhysics depends on FenrirDebugDraw.** Its tick systems draw each world's debug output (bodies, joints, characters, vehicles, as today's categories select) into the world's scene's `DebugDraw`, in a `Physics` group, with duration 0, using `Fenrir::Colour`.
- **The category enum** `DebugDraw` is renamed `PhysicsDebugCategories` in C++ and C#. `SetDebugDraw` keeps its name and takes it; flags such as `Joints` and `Vehicles` keep theirs.
- **Removed:** `PhysicsWorld::DebugLines()`, the packed `DebugLine`, and `DebugColour`'s packed colours.
- **Tests:** physics's debug tests (`DebugLines.cpp`, `CharacterDebugLines.cpp`, `VehicleDebugLines.cpp` and any other users) assert on the scene's `DebugDraw` tessellated batches instead, through FenrirDebugDraw's public interface.
- **Callers follow the rename:** physics's scripting extension and C# contract, its tests, and the legacy DemoApp (behind `FENRIR_BUILD_LEGACY_DEMOAPP`). The legacy DemoApp's `GL_LINES` pass reads the scene's `DebugDraw` batches instead of `DebugLines()`, or is dropped, whichever keeps it building with the least change; it is deleted in ticket 30.
- The physics map (`.scratch/fenrir-physics/map.md`) line for this work points at this ticket.

**Blocked by:** 16 (FenrirDebugDraw: debug shapes, text, durations and groups, headless).

**Touches:** `packages/FenrirPhysics/`, `examples/DemoApp/src/`, `examples/DemoApp/scripts/`, `.scratch/fenrir-physics/map.md`

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests` debug cases pass on `DebugDraw` batches: each category draws the expected lines in the `Physics` group, nothing when its flag is off, and items last until the next tick.
- [ ] Hiding the `Physics` group hides physics's output without stopping recording.
- [ ] No `DebugLine`, `DebugLines()` or packed `DebugColour` remains; `PhysicsDebugCategories` is used in C++ and C#, and physics's scripting tests pass.
- [ ] The legacy DemoApp still builds with `FENRIR_BUILD_LEGACY_DEMOAPP=ON`.
- [ ] Clang and MSVC x64 debug and release presets build with no new warnings.
