# 01: Tracer: the kit's root, the demo panel and the help on FenrirUI

**What to build:** the demo UI kit end to end, thinly. Every demo app gets FenrirUI from the scaffold, a kit root over its own roots, an empty demo panel on the right, and the help at the bottom left in FenrirUI, built from structured key rows, with F1 hiding all of it. ImGui stays where it is.

**Spec:** `.scratch/demo-ui/spec.md`, these sections:
- "Where the kit lives"
- "The kit's root and input"
- "The C++ API", the `DemoUI`, `DemoSection` and `DemoKeyRow` parts this needs
- "What the scaffold feeds the kit", the help
- "The look"

ADR-0071 and ADR-0072 also apply.

- **The kit's sources:**
  - `examples/DemoScaffold/include/FenrirDemoScaffold/DemoUI.hpp` and `src/DemoUI.cpp`, naming no scaffold type.
  - `assets/ui/demo.ui` and `assets/ui/demo.css`, copied with the shared assets.
  - Read from the source tree while it is there, with hot reload on except in smoke runs.
- **The scaffold:**
  - installs FenrirUI (`UI::Install`) after FenrirAssets and before `installPackages`, and with scripting built adds the text, painter and UI scripting extensions
  - adds the `DemoUI` app resource, whose root is at `DemoUILayer` and `DemoUIRouterPriority` (900)
  - sets `textInputByDefault` false whenever the kit is installed
- **UIDemo:** stops calling `UI::Install` and adding those extensions itself (`src/DemoConfig.cpp`, `src/main.cpp` or wherever it adds them), and behaves exactly as before.
- **FenrirUI:** gains `UIRootConfig::gamepadNavigation`, on by default. Off, the root consumes no gamepad input and never publishes the gamepad claim. The kit's root sets it off. Document it in FenrirUI's README.
- **The demo panel:** an empty right-hand panel with app and scene sections (`AppSection`, `SceneSection`), and `ClearScene()` called by the scaffold when a scene is left, before its FenrirScene goes. A section has a heading that collapses it, and `Text` rows only, for now.
- **The help:**
  - `DemoKeyRow`, `DemoScene::keyRows` and `DemoConfig::appKeyRows`.
  - It lists the app and scene (i of n), the scaffold's key rows, the app's, then the scene's.
  - A scene with no `keyRows` shows its `keyHelp` as one row, and an app with no `appKeyRows` shows its `appKeyHelp`, so no demo changes here.
  - It is drawn at the bottom left in the kit, no longer as debug screen text.
  - Showing it still logs the `Help: ...` lines.
- **F1:** shows or hides all of the kit, unless a text entry has the focus. The help section starts open.
- **README:** DemoScaffold's README gains "The demo UI kit" (the root, layers, files and hot reload, the help, F1). Its keys table says F1 hides the kit. The glossary's kit terms stay current.

**Blocked by:** None - can start immediately

**Touches:** `examples/DemoScaffold/`, `examples/UIDemo/src/DemoConfig.cpp`, `examples/UIDemo/src/main.cpp`, `packages/FenrirUI/`

**Status:** resolved

- [ ] Every demo app runs with the kit's help at the bottom left in FenrirUI, and the panel on the right. F1 hides and shows them. ImGui's windows, where an app has them, still draw over the kit and take input first.
- [x] Scaffold tests on the null backend:
  - the help lists the scaffold's, the app's and the scene's key rows in order, and a scene with only `keyHelp` shows it as one row
  - F1 hides the kit and its panels then take no press
  - a press on the panel sends no `DemoSceneClickEvent`, and one beside it does
  - a right-drag beside the panel still turns the free-fly camera
  - a scene section is gone after PageDown
- [x] FenrirUI test: a root with `gamepadNavigation` off, focused, passes the gamepad's directions and A to the game and publishes no gamepad claim.
- [x] `SceneList.cpp`'s help test and every existing scaffold, UIDemo and FenrirUI test pass. Every demo's `Smoke-null` passes with scripting off and on.
- [ ] Editing `examples/DemoScaffold/assets/ui/demo.css` while a demo runs restyles the kit (hand check).
- [ ] Clang and MSVC x64 debug build without new warnings.

## Comments

Built on `demo/01-tracer` (PR into `demo-ui`). Left unticked, for the user:

- The first box and the `demo.css` edit are hand checks on Windows (OpenGL): the kit's help and panel on screen, F1, ImGui's windows over the kit in LightingDemo, RendererDemo's Overlay scene and UIDemo, and an edit to `examples/DemoScaffold/assets/ui/demo.css` restyling the kit. The OpenGL smoke runs of UIDemo, RendererDemo and LightingDemo pass, but nobody looked at them.
- Clang: only `msvc-x64-debug` was built locally (scripting on and off), with no new warnings; `clang-x64-debug` was not built.

`SceneList.cpp`'s help test now taps F1 twice, since the kit starts shown: F1 hides it, and F1 again shows it and logs the help.
