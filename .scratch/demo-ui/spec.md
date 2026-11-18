# Demo UI: every demo on FenrirUI through one demo UI kit in the scaffold, with Razor where it helps, and Dear ImGui removed

Status: built (#384, #388, #390, #391, #395, #396, #397, #398, #399, #400, #401, #402, #406, close-out #407)

## Problem Statement

FenrirEngine has its own UI stack (FenrirText, FenrirPainter, FenrirUI and Razor components, ADR-0060 to ADR-0070), but its demos don't use it:

- LightingDemo, RendererDemo's Overlay scene and UIDemo draw their switches in Dear ImGui, through the demo scaffold's `ImGuiOverlay` (ADR-0027).
- AnimationDemo and PhysicsDemo print their live readouts as debug screen text in the renderer's `DebugFont`.
- Every app's help is one long line of keys in debug screen text.

The user wants ImGui gone from the repository. They want every example and demo on the new UI, Razor included where it is convenient, including the demos that only render text. And they want one reusable thing every demo builds its UI from: key lists, info, tutorials and so on.

`.scratch/demo-ui/research/inventory.md` lists what each demo draws today.

## Solution

The **demo UI kit** (ADR-0071), demo code in `examples/DemoScaffold/`. The scaffold installs FenrirUI and the kit for every demo app.

- **The demo panel** is on the right: the scene switcher, the readouts and the switch rows, and any demo slots.
- **The help** is at the bottom left, built from structured key rows.
- **The demo tutorial** is a card at the top centre.
- **The hint line** sits at the foot of the panel.
- **F1** hides or shows all of it.
- **Authoring:** the kit is native (markup, one stylesheet, a C++ API). C# scenes, and any scene whose switches all have keys, may write their panels as Razor components mounted in a demo slot. The kit ships shared Razor components over the same classes as its C++ rows (ADR-0072).
- **Order of work:**
  1. The kit is built in five tickets.
  2. Each demo moves onto it in tickets of its own, writing its tutorials and its README's panel and hand-check sections.
  3. A last ticket deletes the `ImGuiOverlay` and the submodule and finishes ADR-0027's amendment.

## User Stories

### The kit

1. As someone running a demo, I want one panel on the right listing the app's scenes, so I can click to another scene as well as press PageDown or PageUp.
2. As someone running a demo, I want the scene's live values (clip times, speeds, light counts, draw counts) as labelled rows in the panel, so I can read them without hunting the screen.
3. As someone running a demo, I want the scene's switches (sliders, toggles, choices, colours, buttons, text) in the panel, in the same look in every demo, so a demo is tweaked the same way everywhere.
4. As someone running a demo, I want the keys shown as a table (keys, then what they do) for the scaffold, the app and the scene, so I don't read one long line.
5. As someone new to a demo, I want a short step-by-step tutorial the first time I enter a scene whose controls take learning, which I can step through, close, and reopen from the panel.
6. As someone running a demo, I want a one-line hint for the row under the pointer or with the focus, so a switch's meaning is explained without a tooltip covering the scene.
7. As someone taking a screenshot, I want F1 to hide all of the kit's UI, and show it again.
8. As someone flying the free-fly camera, I want right-drags and clicks outside the panels to reach the scene as before, and a gamepad to always drive the scene.
9. As a demo author, I want a scene's rows, slot and tutorial to go when the scene is left and come back fresh when it is entered, so I never clean them up by hand.
10. As a demo author, I want to change `demo.css` while a demo runs and see every panel restyle, as UIDemo's sheets do.
11. As a demo author writing a C# scene, I want shared Razor components for the kit's rows and a slot in the panel to mount them in, so my panel looks like a C++ one with no native code.
12. As a demo author, I want to write a C++ scene's panel in Razor when that is easier, and have the scene still run with scripting off through its keys, with the slot saying the panel needs scripting.
13. As a demo author, I want choices with more than four options shown as a stepper and colours as a swatch over sliders, since FenrirUI has no dropdown or colour picker.
14. As a demo author, I want a logarithmic range row, since half of LightingDemo's sliders are logarithmic.

### The demos

15. As someone running LightingDemo, I want every switch of today's ImGui "Lighting" window and each scene's section in the demo panel, with the same ranges and the same keys.
16. As someone running LightingDemo's stats-heavy scenes (Sponza, Neon city, Asteroid ring, Forest clearing), I want their stats lines as readouts.
17. As someone running RendererDemo's Overlay scene, I want its label entry, spin slider, state and live input claims in the demo panel, and its 2D test labels drawn by the engine's own text, not debug screen text.
18. As someone running RendererDemo's Painter scene, I want its row captions and status line in painter or FenrirUI text.
19. As someone running RendererDemo's Debug draw scenes, I want debug screen text and messages kept exactly as they are, since they are what the scene tests.
20. As someone running AnimationDemo, I want each scene's playback, blending and crowd readouts in the panel instead of white lines at (16, 120).
21. As someone running PhysicsDemo, I want the named bodies' live readings in the panel instead of a column of text over the messages, and F2's debug-line category shown as a choice row as well as on F2.
22. As someone running UIDemo, I want its switches window (hot reload, stylesheet paths, volume, field of view) in the demo panel, with the HUD and menu still under it.
23. As someone running ScriptingDemo, I want its keys in the help and a tutorial, and a Razor panel proving the C# path.
24. As someone running any demo with C# twin scenes, I want at least one twin's panel written in Razor.

### Engine-wide

25. As the engine's maintainer, I want Dear ImGui, its submodule and the scaffold's `ImGuiOverlay` removed once no demo uses them, with ADR-0027's section rewritten and the engine's comments no longer calling `builtin:2d`'s layout "the ImGui layout".
26. As the engine's maintainer, I want every demo to build and pass its smoke runs with scripting off and on.
27. As the engine's maintainer, I want `DebugFont` left as it is, for engine diagnostics.

## Implementation Decisions

### Where the kit lives

- **Sources:** `examples/DemoScaffold/include/FenrirDemoScaffold/DemoUI.hpp` and `src/DemoUI.cpp`, in namespace `FenrirDemo`, with a README section "The demo UI kit".
- **No scaffold types in the kit's API.** `DemoUI.hpp` includes no `DemoScaffold.hpp`. The scaffold depends on the kit, never the reverse. The kit links FenrirUI, FenrirApp and FenrirInput (ADR-0071).
- **Files:** `examples/DemoScaffold/assets/ui/demo.ui` and `demo.css`, copied into every demo app's asset root with the shared assets (`fenrir_add_demo_app`).
  - While the source tree is there, they are read from it, through a definition of the scaffold's own, as UIDemo's `UIDEMO_SOURCE_ASSETS` does. Hot reload is on except in a smoke run.
  - A demo may load a sheet of its own into the kit's root after `demo.css` (`DemoUI::AddStylesheet`).
- **FenrirUI is installed by the scaffold.** `DemoScaffold::Install` installs FenrirUI after FenrirAssets and before `installPackages`, so every app has it.
  - With scripting built, the scaffold's extensions gain the text, painter and UI ones.
  - UIDemo stops installing FenrirUI itself and stops adding those extensions. A second `UI::Install` would be invalid.

### The kit's root and input

- **The root:** one `UIRoot` on the main window's overlay at `DemoUILayer` with input priority `DemoUIRouterPriority` (constants in `DemoUI.hpp`).
  - **While the ImGui overlay exists,** the kit's layer and priority are 900, under ImGui's 1000 and over every demo's own roots. UIDemo's HUD is at 0 and its menu at 10.
  - The removal ticket keeps 900 and removes ImGui's constants. The number doesn't matter, only that it is above every demo root.
  - Debug screen text and messages still draw above it (ADR-0071).
- **Pointer input:** the root's elements are `pointer-events: none` except the panels' boxes (`demo.css`). So the free-fly camera's right-drag and the scene click pass everywhere else (ADR-0065). A press on a panel is consumed, so it sends no `DemoSceneClickEvent`.
- **No gamepad navigation.** Ticket 01 adds a `UIRootConfig::gamepadNavigation` switch to FenrirUI (on by default; off, the root never consumes gamepad input and never publishes the gamepad claim, even while it has the focus), and the kit's root sets it off. This is the spec's one engine change. Its C# `RootSettings` mirror is left for FenrirUI's follow-ups unless the binding generator picks it up for free.
- **Keyboard:**
  - Tab and mouse focus the kit's controls as FenrirUI does.
  - A focused text entry takes the keyboard (the `keyboardFocus` claim cancels the scene's held keys).
  - Escape blurs it, and a click on the scene gives the keyboard back.
  - F1 toggles the kit even while it has the focus, unless a text entry is focused.
- **Text input:** `WindowConfig::textInputByDefault` is false whenever the kit is installed, so the window's text input follows the kit's `textInput` claim.

### The C++ API (`DemoUI.hpp`)

The shape is the tracer's to settle within these lines. Names below are the spec's working names.

- **`DemoUI`**, an app resource the scaffold adds:
  - `Root()`: its `UIRoot`.
  - `SetVisible(bool)` and `IsVisible()`: F1.
  - `AddStylesheet(path)`.
  - `AppSection(title)` and `SceneSection(title)`: a `DemoSection` in the demo panel. App sections stay. Scene sections are removed by `ClearScene()`, which the scaffold calls when a scene is left, before its FenrirScene is destroyed.
  - `SceneSlot(id)`: a demo slot.
  - `SetHelp(...)`.
  - `ShowTutorial(...)`.
- **`DemoSection`**, a handle, adds rows in order:
  - `Readout(label)` returns a `DemoReadout` with `Set(std::string_view)`. A convenience `Readout(label, std::function<std::string()>)` is pulled each frame.
  - `Toggle(label, DemoValue<bool>, hint = "")`.
  - `Range(label, DemoValue<float>, min, max, DemoRangeOptions{logarithmic, step, decimals, angle}, hint)`, and an integer `Range` over `DemoValue<int>`.
  - `Choice(label, DemoValue<int>, std::vector<std::string> options, hint)`: a row of buttons for up to four options, a previous and next stepper over the option's name beyond that.
  - `Colour(label, DemoValue<Fenrir::Math::Vec3> linear, hint)`: a swatch over three sliders, hue, saturation and value. The swatch shows the linear colour encoded for display.
  - `Button(label, std::function<void()>, hint)`.
  - `TextEntry(label, DemoValue<std::string>, hint)`.
  - `Text(line)`: a read-only line, which can be updated.
  - `Separator(title)`.
  - Each returns a row handle that can be shown or hidden (`SetShown`) and disabled.
- **`DemoValue<T>`** binds a row to the scene's state, either by a pointer the scene owns or by a get and set pair.
  - **Pointers:** a scene-owned row is cleared before the scene's state goes, so a pointer into the scene's state is safe.
  - **Each frame**, in the kit's build stage work (`UIRoots::AddBuildStage`), the kit reads each value and writes it into the control, unless the user is dragging or editing that control. A Change from a control is written back through the binding, the frame it arrives, before the scenes' `Update`.
  - **So keys and the panel agree:** a key that changes a value moves the control the next frame, and a control that changes it changes what the keys see.
- **Logarithmic ranges** map the slider's 0..1 to `min * (max / min)^t` in the kit. `min` must be positive; the kit logs an error otherwise.
- **`DemoKeyRow { std::string keys; std::string what; }`** is the structured help line.
- **Hints:** a row's hint shows in the hint line while it is hovered or holds the focus. The kit tracks this from UI events (hover enter and leave, focus and blur) and clears the line when neither holds.

### What the scaffold feeds the kit

- **`DemoScene` gains:**
  - `std::vector<DemoKeyRow> keyRows`: the scene's help.
  - `std::optional<DemoTutorial> tutorial`.
  - `bool razorPanel`: the scene mounts a Razor panel in its slot; without scripting the slot shows the note.
  - `DemoScene::keys`, what a smoke run presses, stays as it is.
  - `keyHelp` stays until the removal ticket. While it is there, a scene with no `keyRows` shows its `keyHelp` as one row, so the tracer changes no demo.
- **`DemoConfig` gains:**
  - `std::vector<DemoKeyRow> appKeyRows`, the app's F2 and anything app-wide, which replaces `appKeyHelp` the same way.
  - `bool frameStats`: the panel's top section shows frame time and fps (smoothed) and draws and instances from `Renderer::Stats`.
- **The help panel:**
  - **What it shows:** the app's name and the scene (i of n), then the scaffold's own key rows (PageDown/PageUp, F1, F3, the free-fly keys and the scene click), then the app's, then the scene's.
  - **Where:** bottom left. It is a kit section that starts open, collapsible by its heading.
  - **Logging:** showing it still logs it as `Help: ...` lines, so `SceneList.cpp`'s help test keeps working.
  - **Debug screen text:** the scaffold's own help is no longer drawn as debug screen text.
- **The scene switcher** is the panel's first app section: one button per scene, the active one marked (`:checked`-like class `active`). A click enters that scene the next frame, as PageDown does.
- **Messages:** on-screen messages stay the renderer's, stacked from the top-left. The kit's panels are on the right and bottom so they never cover them.

### Demo tutorials

- **`DemoTutorial { std::string title; std::vector<DemoTutorialStep> steps; }`**.
- **`DemoTutorialStep { std::string title; std::string text; std::vector<DemoKeyRow> keys; std::function<bool(Fenrir::App&)> done; }`**:
  - `done`, when given, is asked each frame while its step shows. True moves to the next step (or closes the tutorial after the last).
  - Next and Back buttons step by hand.
  - Close and Escape close it.
- **Showing:**
  - A scene's tutorial shows the first time the scene is entered in a run.
  - It never shows in a smoke run or with `FENRIR_DEMO_KEYS`.
  - It shows again from a "Tutorial" button in the scene's section.
  - The card is at the top centre, under any messages' column, and takes pointer input on its box only.
- **Which scenes get one:**
  - every app's first scene: three to five steps on the app and the scaffold's keys
  - PhysicsDemo's Character and Vehicles
  - UIDemo's room
  - AnimationDemo's Blending
  - LightingDemo's Many lights
  - any other scene a demo ticket finds worth it

### C# and Razor (ADR-0072)

- **`Fenrir.Demo` gains plain C#, no natives:**
  - `DemoUI.Layer` (the kit's layer).
  - `DemoUI.TryGetRoot(out UIRoot root)` (`UIRoot.Find` on the main window at that layer).
  - `DemoUI.TryGetSceneSlot(string id, out UIRoot root, out ElementId slot)`.
  - `Fenrir.Demo.csproj` references the `Fenrir.UI` contract.
- **Shared Razor components** are under `examples/DemoScaffold/Managed/Fenrir.Demo.Razor/`: `DemoSection`, `Readout`, `Toggle`, `Range` (with `Logarithmic`), `Choice` and `ColourRow`.
  - They use `demo.css`'s classes and two-way `@bind` (ADR-0069).
  - `fenrir_add_demo_app` adds `Fenrir.Demo.Razor.props` to every scripts project. It imports `Fenrir.UI.Razor.InRepo.props` and compiles those `.razor` files into the app's script with the Razor dispatcher (ADR-0068).
  - A scripts project with no `.razor` of its own still builds.
- **Mounting:** a C# scene mounts its Razor panel into its slot from a system's `OnCreate` and again for each `CSharpScene.Entry`, as UIDemo's speedometer does. The slot goes when the scene is left, which unmounts it.
- **C# scenes' tutorials** are on their C++ `DemoScene` entries, so the kit needs no `TutorialSteps` component.
- **Razor in a C++ scene:**
  - The scene sets `razorPanel` and its C# side mounts into the slot.
  - With scripting off, the slot shows "This panel is a Razor component and needs scripting (FENRIR_ENABLE_SCRIPTING); its keys are in the help."
  - Every switch on such a panel must have a key in the scene's key rows; the ticket's review checks it.

### The look (`demo.css`)

- **One theme:** dark translucent panels, the kit's custom properties at the top (`--demo-accent`, `--demo-ink`, `--demo-panel` and the rest), FenrirText's embedded default face (JetBrains Mono).
- **Sizes:** the panel is about 360 UI units wide at the window's default `Height1080` scale and scrolls when taller than the window.
- **Collapsing:** sections collapse by their heading button.
- **The scripting-off note** is a muted line in the slot's place.

### Per-demo moves

- **UIDemo (ticket 06):**
  - `ShowSwitches` becomes an app section: two toggles for hot reload, two read-only lines for the stylesheet paths, and readouts for volume and field of view.
  - The six key lines become `keyRows`, and the room gets a tutorial.
  - `HudLayer`, `MenuLayer` and `MenuInputPriority` lose their "under ImGui's" comments.
  - The terminal's ray check keeps reading the `pointer` claim, now the kit's.
  - `DemoConfig.cpp` drops `imguiOverlay` and its own `UI::Install`.
- **RendererDemo (ticket 07):**
  - **Overlay:** its ImGui window becomes a scene section: a text entry feeding the label, a spin-speed range, readouts for its state and for the four input claims.
  - **Overlay and Painter labels:** they become painter text in the scene's own lists, or a small FenrirUI root of the scene's under the kit. Either way they are drawn by FenrirText, not debug screen text.
  - **Debug draw scenes** keep their screen text and messages and gain key rows.
  - **Razor:** a C# twin (Meshes and materials (C#), or Debug draw (C#)) gets a Razor panel.
  - **Tutorial:** on Meshes and materials, the first scene.
- **AnimationDemo (ticket 08):**
  - Every `ScreenText` readout (C++ and C#) becomes readouts.
  - **Razor:** Playback (C#) and Bone attachments (C#) mount Razor readout panels.
  - Skin diagnostics' static line becomes a `Text` row.
  - **Tutorials:** the first scene and Blending.
- **PhysicsDemo (ticket 09):**
  - `ShowReadouts` (C++ `SceneLooks.cpp`, C# `scripts/Looks.cs`) becomes readouts, one per named entity, in a scene section.
  - **F2's** debug-line category is also an app choice row, kept in step with F2.
  - **Razor:** the C# twins (Character, Joints, Vehicles) mount Razor panels.
  - The long key help lines become key rows, gamepad included.
  - **Tutorials:** Playground (first), Character and Vehicles.
- **LightingDemo (tickets 10 and 11):**
  - **Ticket 10:** `LightingPanel` becomes kit sections, an app-level set the scenes share, with every row, range, logarithmic flag and key of today's window:
    - the tonemapper and debug view are choices
    - "Show reflections or confidence" is a choice of two
    - the grade tints are colour rows
    - "No grade" is a button
    - the panorama is a choice, or a read-only line when there is none
    - the sky rotation is an angle range
  - **Ticket 10 also moves** Material grid, Showcase models, Showroom and Many lights (C++ and C#) onto the kit. Many lights (C#) mounts a Razor panel for its scene section.
  - **Ticket 11:** Sponza, Neon city, Asteroid ring and Forest clearing, whose stats lines become readouts. After it, LightingDemo sets no `imguiOverlay`.
  - **Tutorials:** the first scene and Many lights.
- **ScriptingDemo (ticket 12):** key rows and a tutorial for Showcase. Ticket 05's proof panel stays.

### Removal (ticket 13)

**Deleted:**
- `src/ImGuiOverlay.hpp/.cpp`, `ImGuiOverlayLayer`, `ImGuiOverlayRouterPriority` and `DemoConfig::imguiOverlay`
- the `textInputByDefault` coupling, which now follows the kit
- `examples/DemoScaffold/libs/` and the `examples/DemoScaffold/libs/imgui` submodule (`.gitmodules` and `git rm`)
- `Tests/ImGuiOverlay.cpp` and `DemoTestApp`'s flag
- `DemoScene::keyHelp` and `DemoConfig::appKeyHelp`

**Rewritten:**
- the engine's comments that say "the ImGui vertex layout" (`DrawList2D.hpp:44,321`, `2d.vert`, `debug_text.vert`, `BuiltinShaders.hpp`, `DebugFeature.cpp`, `Tests/DrawLists2D.hpp`, FenrirRenderer's `CMakeLists.txt:212`), to "position, UV, RGBA8"
- ADR-0027's "ImGui in the demos" section (its amendment says what was removed)
- the glossary's *demo scaffold* entry
- the README, DemoScaffold's and FenrirDraw2D's READMEs where they name ImGui

## Testing Decisions

- **Kit tests** in `FenrirDemoScaffoldTests`, on the null backend through `DemoTestApp`, its root drawing into a capturing target as UIDemo's tests do. They replace the five ImGui tests by the removal ticket:
  - a press on a panel sends no `DemoSceneClickEvent`
  - a drag on a range row changes its value and doesn't turn the free-fly camera
  - a text entry takes typed text and claims the keyboard, cancelling a held scene key; a click on the scene gives the keyboard back
  - F1 hides the kit, and its panels then take no press
  - the help lists the scaffold's, the app's and the scene's key rows in order
  - a scene's section is gone after PageDown and back after PageUp
  - a bound value changed by code shows in its control the next frame, and a Change writes it back
  - a logarithmic range maps its ends and middle as `min * (max/min)^t`
  - a choice of five shows a stepper, and of three, buttons
  - a tutorial shows on first entry, not on the second or in a smoke run, and steps on `done`
  - the gamepad's directions with the kit focused reach the game
  - FenrirUI's `gamepadNavigation` switch gets a test of its own in FenrirUI's tests
- **C#:** ScriptingDemo's smoke runs with scripting on mount the Razor proof panel without errors. Each demo's C# twin's smoke run does the same for its panel.
- **Every demo** builds and passes `<App>-Smoke-null` with scripting off and on. UIDemo's existing tests keep passing with the kit installed.
- **Hand checks:** each demo ticket rewrites its README's hand checks for the panel, and the user runs them on Windows (OpenGL) before merging. They are not automated.
- **Build:** MSVC x64 debug locally (the memory rule); CI builds msvc-x64-release.

## Out of Scope

- **Moving `DebugFont`** (debug screen text and on-screen messages) onto FenrirText.
- **New FenrirUI controls:** a `select` dropdown, a colour picker, popups or anchored tooltips. These go to `.scratch/fenrir-ui-followups/` or later. The kit builds composites.
- **A log or messages area in the kit.**
- **Moving the kit into an engine package.** That is a later decision, if a game wants it.
- **Natives on `Fenrir.Demo`.**
- **Saving panel state between runs.**
- **Screenshots in READMEs.**

## Further Notes

- **The FenrirUI follow-ups** (PR #380, `.scratch/fenrir-ui-followups/`): its ticket 02 moves `YawPitch` and the screen quad into DemoScaffold and touches `examples/DemoScaffold/`, UIDemo's `Room.cpp` and RendererDemo. Whichever merges second rebases. Its `cursor` ticket would let the kit's buttons show a hand cursor for free.
- **Ticket order:**
  - **01 to 05** all touch `examples/DemoScaffold/`, so they are one stack: 01, 02, 03, 04, 05.
  - **The demo tickets 06 to 12** each touch their own demo, and branch from the kit stack's tip once 05 is merged into the integration branch, side by side. Ticket 11 stacks on 10, since both edit LightingDemo's README. Ticket 12 follows 05, whose ScriptingDemo files it shares, so it starts once 05 is merged.
  - A demo ticket that finds a kit gap fixes it in `examples/DemoScaffold/` and says so in its PR. Two such fixes at once stack.
  - **Ticket 13** waits for every other ticket.
- **Integration branch:** `demo-ui`, off `main`. Each ticket's PR goes into it, and the trunk PR into `main` describes the whole stack.

## Tickets

1. [01: tracer: the kit's root, the demo panel and the help on FenrirUI](issues/01-tracer-the-kit-root-the-demo-panel-and-the-help-on-fenrirui.md)
2. [02: readouts, frame stats and the scene switcher](issues/02-readouts-frame-stats-and-the-scene-switcher.md)
3. [03: switch rows and the hint line](issues/03-switch-rows-and-the-hint-line.md)
4. [04: demo tutorials](issues/04-demo-tutorials.md)
5. [05: C# and Razor: the slots and the shared Razor components](issues/05-csharp-and-razor-the-slots-and-the-shared-razor-components.md)
6. [06: UIDemo on the kit](issues/06-uidemo-on-the-kit.md)
7. [07: RendererDemo on the kit](issues/07-rendererdemo-on-the-kit.md)
8. [08: AnimationDemo on the kit](issues/08-animationdemo-on-the-kit.md)
9. [09: PhysicsDemo on the kit](issues/09-physicsdemo-on-the-kit.md)
10. [10: LightingDemo's shared panel and its first scenes on the kit](issues/10-lightingdemo-shared-panel-and-first-scenes-on-the-kit.md)
11. [11: LightingDemo's large scenes on the kit](issues/11-lightingdemo-large-scenes-on-the-kit.md)
12. [12: ScriptingDemo on the kit](issues/12-scriptingdemo-on-the-kit.md)
13. [13: Dear ImGui removed](issues/13-dear-imgui-removed.md)
