# 05: C# and Razor: the slots and the shared Razor components

**What to build:** a C# scene, or a C++ scene with a Razor panel, mounts a Razor component into a demo slot in the panel, built from shared Razor components that look like the C++ rows. ScriptingDemo proves it with a Razor panel. With scripting off, the slot says the panel needs scripting.

**Spec:** `.scratch/demo-ui/spec.md`, "C# and Razor". ADR-0072 and ADR-0067 to ADR-0069 also apply.

- **Demo slots:** `DemoUI::SceneSlot(id)` and `DemoScene::razorPanel`. The slot is an element with a known id in the scene's section. It goes when the scene is left. Without scripting built, it shows the muted note line.
- **`Fenrir.Demo`, plain C# with no natives:**
  - `DemoUI.Layer`
  - `DemoUI.TryGetRoot(out UIRoot)`
  - `DemoUI.TryGetSceneSlot(string id, out UIRoot, out ElementId)`
  - `Fenrir.Demo.csproj` references the `Fenrir.UI` contract, and its CMake build depends on it.
- **Shared Razor components** in `examples/DemoScaffold/Managed/Fenrir.Demo.Razor/`: `DemoSection`, `Readout`, `Toggle`, `Range` (`Logarithmic`, `Step`, `Decimals`), `Choice` and `ColourRow`.
  - They are written over `demo.css`'s classes and use `@bind`.
  - A component whose behaviour must match a C++ row (a stepper beyond four options, the logarithmic mapping, the hint line) matches it.
- **The props file:** `Fenrir.Demo.Razor.props` imports `Fenrir.UI.Razor.InRepo.props` and adds those `.razor` sources to the project. `fenrir_add_demo_app` adds it to every scripts project, and a project with no `.razor` of its own builds as before.
- **ScriptingDemo:**
  - Showcase gets a Razor panel in its slot, mounted from a C# system's `OnCreate` and again for each entry: a `Toggle` for the boxes' spin direction, a `Range` for the spin speed, a `Readout` of the sparks alive.
  - Each switch is also on a key the help lists (R already reverses; a new key for speed if needed).
  - The C# component `Spin` reads them through a component or resource it already uses, or a new one in its scripts.
- **README:** DemoScaffold's README "C# scenes" and "The demo UI kit" document slots, the Razor components, the props file and the scripting-off note.

**Blocked by:** 04

**Touches:** `examples/DemoScaffold/`, `examples/ScriptingDemo/scripts/`, `examples/ScriptingDemo/src/Showcase.cpp`

**Status:** ready-for-agent

- [ ] With scripting built, ScriptingDemo's Showcase shows its Razor panel in the demo panel, styled like the C++ rows; the toggle reverses the spin and the range changes its speed (hand check). `ScriptingDemo-Smoke-null` passes with no error.
- [ ] Built without scripting, Showcase's slot shows the note and the scene works by its keys. `Smoke-null` passes.
- [ ] Every other demo's scripts project builds unchanged with the props added, and every `Smoke-null` passes with scripting off and on.
- [ ] A scaffold test: a scene with `razorPanel` has its slot's element by id while active, and not after it is left.
- [ ] Clang and MSVC x64 debug build without new warnings; the C# builds with no new warnings.
