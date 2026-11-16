# 13: Dear ImGui removed

**What to build:** with no demo using it, Dear ImGui leaves the repository: the scaffold's `ImGuiOverlay`, its switch, its tests, its build and its submodule. The one-line key help gives way to key rows everywhere, and the engine's comments and docs stop calling `builtin:2d`'s layout ImGui's.

**Spec:** `.scratch/demo-ui/spec.md`, "Removal". ADR-0071 and ADR-0027's amendment also apply.

- **Deleted:**
  - `examples/DemoScaffold/src/ImGuiOverlay.hpp/.cpp`, `ImGuiOverlayLayer`, `ImGuiOverlayRouterPriority` and `DemoConfig::imguiOverlay`, and its handling in `DemoScaffold.cpp` (BeforeTicks, the PostUpdate system, teardown)
  - the `textInputByDefault` coupling, which now follows the kit alone
  - `examples/DemoScaffold/libs/` (`FenrirDemoImGui`) and the `libs/imgui` submodule (`git rm`, `.gitmodules`), with the scaffold's CMake links
  - `examples/DemoScaffold/Tests/ImGuiOverlay.cpp` and `DemoTestApp`'s `imguiOverlay` flag. The kit's tests from tickets 01 and 03 cover what it tested; add any that is missing.
  - `DemoScene::keyHelp` and `DemoConfig::appKeyHelp`, and the kit's fallback for them. A demo still using them is moved to key rows here.
- **Rewritten:** the comments naming the ImGui vertex layout:
  - `packages/FenrirDraw2D/include/FenrirDraw2D/DrawList2D.hpp` (the `Gamma` and `Vertex2D` comments)
  - `packages/FenrirRenderer/Shaders/Builtin/2d.vert`, `debug_text.vert`, `src/Core/BuiltinShaders.hpp`, `src/Core/DebugFeature.cpp` and `Tests/DrawLists2D.hpp`
  - FenrirRenderer's `CMakeLists.txt`

  They now say "position, UV, RGBA8". `builtin:2d` and `Vertex2D` stay.
- **Docs:**
  - ADR-0027's "ImGui in the demos" is rewritten to record that ImGui served the demos until the demo UI kit (ADR-0071) and was removed, under the existing amendment.
  - The glossary's *demo scaffold* entry says "the demo UI kit" for "the optional ImGui overlay".
  - The repository README, DemoScaffold's README (its ImGui section goes) and FenrirDraw2D's and FenrirRenderer's READMEs lose ImGui except as history.
- **A grep:** `imgui` (case-insensitive) outside `docs/adr/` and `.scratch/` finds nothing.

**Blocked by:** 06, 07, 08, 09, 11, 12

**Touches:** `examples/DemoScaffold/`, `.gitmodules`, `packages/FenrirDraw2D/`, `packages/FenrirRenderer/`, `docs/adr/0027-ui-draws-through-fenrirdraw2d-and-takes-input-through-input-routers.md`, `GLOSSARY.md`, `README.md`, `examples/*/src/` (any `keyHelp` left)

**Status:** ready-for-agent

- [ ] A fresh clone with `git submodule update --init --recursive` fetches no ImGui. The build and every test pass, with examples on, scripting off and on.
- [ ] The scaffold's tests cover:
  - a press on a panel sending no scene click
  - a range drag not turning the camera
  - text entry claiming and giving back the keyboard
  - F1 hiding the kit
  - the help's key rows
- [ ] Every demo's `Smoke-null` passes.
- [ ] Hand check, on OpenGL: each demo app opens, shows its panel and help, and F1 hides them.
- [ ] Clang and MSVC x64 debug build without new warnings.
