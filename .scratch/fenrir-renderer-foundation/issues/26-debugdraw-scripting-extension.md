# 26: The Fenrir.DebugDraw scripting extension

**What to build:** C# scripts draw everything C++ can into the active scene's `DebugDraw`, and physics's C# contract keeps only `SetDebugDraw` with `PhysicsDebugCategories`; RendererDemo gains "Debug draw (C#)" (spec: "Scripting: the extensions" (`Fenrir.DebugDraw`, no-op rules), "Demo apps" (RendererDemo scene 6), "Testing Decisions" seam 7 (debug draw from C#); ADR-0029, ADR-0025).

- **Layout:** contract in `packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/`, native functions in a static `DebugDrawScripting` library; builds with `FENRIR_ENABLE_SCRIPTING` (FenrirDebugDraw is always built); `DebugDraw::ScriptingExtension()` (or the owner's name from ticket 16).
- **A static `DebugDraw` facade** with every C++ call: shapes wire and solid, text, messages, durations including `Forever`, groups (show, hide, clear), `relativeTo`, depth mode and thickness, taking `Fenrir.Math` types and `Colour`. It is the only C# path to debug drawing.
- **Physics's contract** keeps only `SetDebugDraw` with `PhysicsDebugCategories` (anything else debug-related in it goes).
- **No-op rules:** no scene or a stale `relativeTo` entity → nothing; one debug warning per call site; nothing throws.
- **RendererDemo scene 6b, "Debug draw (C#)":** a twin of scene 4 drawn from C#. Hand checks in the README.

**Blocked by:** 17 (The renderer draws debug draw: lines, solids, world text and screen text); 18 (FenrirPhysics draws its debug output through FenrirDebugDraw); 25 (The Fenrir.Renderer scripting extension and C# demo scenes).

**Touches:** `packages/FenrirDebugDraw/Managed/`, `packages/FenrirDebugDraw/Scripting/`, `packages/FenrirPhysics/Managed/`, `examples/RendererDemo/`, `CMakeLists.txt`, `FenrirScripting.sln`

**Status:** resolved

Hand check: RendererDemo's "Debug draw (C#)" looks the same as scene 4, "Debug draw", shape for shape, and its keys (1, 2, 3, T, F, X) do the same (examples/RendererDemo/README.md).

- [x] Extension tests: shapes, text and messages drawn from a C# system appear in the scene's tessellated batches the same as the C++ calls; durations from a C# tick system last until the next tick; groups toggle and clear from C#.
- [x] Physics's C# contract exposes `SetDebugDraw(PhysicsDebugCategories)` and nothing else debug-related, and physics scripting tests pass.
- [x] A stale `relativeTo` entity no-ops with a debug warning.
- [x] `FENRIR_DEMO_SMOKE` runs scene 6b on null and GL with no errors, with scripting on.
- [ ] Scene 6b by hand: it matches scene 4 (checked by hand).
