# 30: Delete the legacy DemoApp and FenrirCamera

**What to build:** No placeholder code remains: with RendererDemo, ScriptingDemo and PhysicsDemo covering every legacy scene and passing their smoke runs, `examples/DemoApp` is deleted with everything only it used (spec: "Demo apps" (deleting `examples/DemoApp`), "Build" (examples options), "Cameras, render targets and sky" (`FenrirCamera`); ticket 23).

- **Deleted:** `examples/DemoApp` (sources, scripts, assets not moved to `examples/assets/`, `libs` with the ImGui editor, ImGuizmo, assimp and v8), the DemoApp's input structs, `packages/FenrirCamera`, and the `FENRIR_BUILD_LEGACY_DEMOAPP` option.
- **Checked first:** every legacy scene has a home (Showcase in ScriptingDemo; Default and every physics scene in PhysicsDemo), and nothing outside the DemoApp includes `FenrirCamera` or uses the legacy assets.
- **README:** the "before opening a PR" smoke-run line rewritten for the new apps; options table without the legacy option; any remaining DemoApp references (README, docs, `AGENTS.md`, `CODING_STANDARDS.md`, the solution file) updated.
- The renderer map's foundation line (`.scratch/fenrir-renderer/map.md`) notes the DemoApp is gone. Recording the spec built, and deleting `.scratch/fenrir-renderer-foundation/`, follows the issue tracker's "Built" rule when this ticket merges.

**Blocked by:** 20 (The ImGui overlay and RendererDemo's Overlay scene); 27 (ScriptingDemo: the scripting showcase ported); 29 (PhysicsDemo: the vehicle scenes).

**Touches:** `examples/DemoApp/`, `examples/CMakeLists.txt`, `packages/FenrirCamera/`, `CMakeLists.txt`, `README.md`, `FenrirScripting.sln`, `.gitmodules`, `.scratch/fenrir-renderer/map.md`

**Status:** ready-for-agent

- [ ] No `examples/DemoApp`, `packages/FenrirCamera` or `FENRIR_BUILD_LEGACY_DEMOAPP` remains, and a repository search finds no stale reference to them outside history and `.scratch`.
- [ ] Every preset configures and builds with examples on, and CI's smoke runs for all three demo apps pass on null everywhere and on llvmpipe on Linux.
- [ ] The README's PR smoke-run line names the new apps and switches.
