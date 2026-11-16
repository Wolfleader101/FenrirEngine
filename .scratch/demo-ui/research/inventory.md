# What the demos draw today: ImGui, debug screen text and the help

A survey on 2026-11-16 of `main` at `10f8b07d`, for `.scratch/demo-ui/spec.md`. Line numbers are as of that commit.

## Who uses what

- **ImGui on** (`config.imguiOverlay = true`):
  - LightingDemo (`src/main.cpp:26`)
  - RendererDemo (`src/main.cpp:28`, the Overlay scene only)
  - UIDemo (`src/DemoConfig.cpp:71`)
- **ImGui off:** AnimationDemo, PhysicsDemo and ScriptingDemo.
- **No C# script calls ImGui.**
- **What the ImGui code uses:** sliders (some logarithmic, one an angle), integer sliders, checkboxes, combos, radio buttons, two `ColorEdit3`, one `InputText`, one button, separators and text lines.
- **What it does not use:** plots, tables, tooltips, drag floats or collapsing headers. The only stats are text lines from `Renderer::Stats` and a smoothed frame time.

## DemoScaffold (`examples/DemoScaffold/`)

**Frame (`src/DemoScaffold.cpp`):**
- **BeforeTicks** (642-679): `imgui->BeginFrame()`, then `Start()` on the first frame, PageDown/PageUp, F3, `UpdateHelp`, `UpdateFreeFly`, `UpdateClick` and smoke keys.
- **Update** (682): the scene's `update`, which is where ImGui windows are made.
- **PostUpdate** (809-815), with ImGui only: `imgui->EndFrame()` submits ImGui's list on `ImGuiOverlayLayer` (1000).
- **Text input:** `window.textInputByDefault = !config.imguiOverlay` (747).
- **Teardown:** ImGui is made last and destroyed first (204, 788).

**F1 help (`HelpLines` 451-466, `UpdateHelp` 468-517):**
- **Drawn as** debug screen text (`draw->ScreenText`, renderer `DebugFont`), bottom-left, margin 16, line height 20. Lines wrap at 100 characters.
- **Logged** as `Help: ...` when shown.
- **Lines:**
  - `"<app>: <scene> (i/n)"`
  - `"Scene keys: " + keyHelp`
  - `"F2: " + appKeyHelp`
  - the scaffold's keys
  - the free-fly line
- **On entering a scene** it logs `"Scene i/n: name. F1 for help."` (364).
- **On-screen messages:** the scaffold has none of its own. Scenes send them with `DebugDraw::Message`, and the renderer stacks them from the top-left.

**Input:**
- **`ImGuiOverlay` (`src/ImGuiOverlay.cpp`)** is a router of priority 1000:
  - keys go to ImGui on `WantCaptureKeyboard`, text on `WantTextInput`, presses and the wheel on `WantCaptureMouse` while the game holds no button
  - motion passes unless ImGui owns a drag
  - it publishes the `pointer`, `pointerCapture`, `keyboardFocus` and `textInput` claims
- **The scaffold reads no claims.** `UpdateFreeFly` (519) and `UpdateClick` (568) rely on consumption.
- **UIDemo `Room.cpp:389-399`** casts the terminal's ray only without the `pointer` claim.
- **UIDemo's layer constants** say "under ImGui's" (`UIDemo.hpp:63,155,158`).

**Tests (`Tests/`):**
- **`ImGuiOverlay.cpp`** has five cases, all ImGui:
  - a click on the window sends no scene click (130)
  - typing claims text input (152)
  - focusing a field cancels a held key (184)
  - clicking away gives the keyboard back (205)
  - a right-drag in ImGui doesn't turn the camera (225)
- **`DemoTestApp.hpp:124,162-168`** has an `imguiOverlay` flag.
- **`SceneList.cpp:188`** checks the logged help lines only.
- **UIDemo's tests** run through `MakeDemoConfig`, so with ImGui.

**Build:** `CMakeLists.txt:15-18` links `FenrirDemoImGui` and `FenrirDraw2D` PUBLIC. `libs/CMakeLists.txt` builds the `libs/imgui` submodule (v1.92.9b).

## LightingDemo (ImGui; no screen text)

**`src/LightingPanel.hpp/.cpp`:**
- **`LightingSettings`:** look, AO, reflections, debug view, sun shadows, panoramas, sky intensity and rotation.
- **`LightingKeyHelp(bool panoramaSwitch)`** (317).
- **`RunLightingPanel(app, scene, settings, sceneControls)`** (330), which runs ApplyKeys, then DrawWindow, then Apply.
- **ApplyKeys (78-129):** T tonemapper, E exposure, B bloom, O AO, G debug view, H sun shadows, S panorama.
- **DrawWindow (131-264)**, window "Lighting" at (16,16), width 340:
  - **Camera:** exposure -6..6; tonemapper combo (AgX, PBR Neutral, ACES, None); bloom intensity (log) and radius; streak intensity (log) and length; flare (log); lens dirt.
  - **Grade:** temperature 2500..25000 (log); tint; contrast (log); shadow and highlight tint (`ColorEdit3`); balance; saturation; a "No grade" button.
  - **Camera effects:** chromatic aberration; vignette and its radius; film grain.
  - **GTAO:** on; radius (log); intensity.
  - **SSR:** on; intensity; max distance (log); thickness (log); roughness cutoff; radio "Show reflections" or "Show confidence".
  - **Sun shadows:** on; distance (log); normal bias; slope bias.
  - **Debug view:** a combo of 8, with a legend for light clusters.
  - **Sky:** a panorama combo, or "No panorama loaded"; intensity; rotation (an angle).
  - **Scene:** the scene's callback.
- **Apply (266-306)** writes the settings to every Camera, every DirectionalLight and the Sky.

**Scenes:**
- **Material grid:** toggle sun (L); base colour (`ColorEdit3`).
- **Showcase models:** toggle turntable (R).
- **Many lights:** lights 0..max (int); moving (P); point and spot intensity; range; spot shadows (K) and a budget 0..16; point shadows (J) and a budget 0..8; clustered (C); text lines.
- **Many lights (C#):** the C++ `update` runs the panel with three text lines. V cycles budgets.
- **Showroom:** per rect light, width, height, intensity and two-sided (V).
- **Sponza:**
  - text: frame ms/fps; draws, instances and shadow draws
  - vsync; sun intensity; torches lit (K), torch shadows (J), flicker (N); torch intensity and range
- **Neon city:**
  - text: frame; light counts; buildings, panels and cars; draws and instances
  - clustered (C) and vsync; traffic (N); puddles (M) 0..1; reflections (R)
  - lamp, sign, headlight and uplight intensities
- **Asteroid ring:**
  - rocks (N) 1000..max; automatic instancing (I); ring turning (R)
  - text: draws, instances, drawn, uploads and frame
  - GPU culling (C), occlusion (V), freeze (F); LOD error in pixels (L, log)
  - text: drawn by LOD (8) and by phase
- **Forest clearing:** density 5..100% (N); grass (M); text: copies, lists, trees, blades, chunks, draws and frame.

**Other:** `FetchedAssets.cpp:58` shows a keyed orange `Message` when the downloaded assets are missing; that stays a message. F2 does nothing in this app.

## RendererDemo (ImGui in Overlay only)

- **Overlay (`OverlayScene.cpp`):**
  - ImGui window "Overlay" (192-209), holding:
    - an explanation
    - an `InputText` "Label" that feeds the cube's `WorldText` (259)
    - spin speed 0..5
    - state text
    - the live input claims
  - Labels drawn as `ScreenText`:
    - "Layers: sent 2, 1, 0" (102)
    - "Scissors" (117)
    - the blend-mode names (141)
    - the blend title (144)
  - It draws its own lists with `builtin:2d`.
- **Painter (`PainterScene.cpp`):** row captions as `ScreenText` (71, 106, 139) and a status line (198).
- **Debug draw (C++ `DebugDrawScene.cpp:232`, C# `scripts/DebugDrawScene.cs:92`):** `ScreenText` and messages are the feature under test, so they stay.
- **Meshes and materials, Cameras and render targets, Sky:** keys only.
- **F2:** nothing.

## UIDemo (ImGui switches window)

- **`Room.cpp` `ShowSwitches` (314-347):** window "UIDemo", holding:
  - six key lines
  - hot reload toggles for the HUD and the menu, each with its stylesheet path
  - read-only volume and field of view
- **No screen text.**

## AnimationDemo (no ImGui; screen-text readouts from (16,120), 24 apart)

| Scene | Readouts |
| --- | --- |
| Playback (`Playback.cpp:214`) | Clip, time and duration, speed, loop, playing and laps. A message at the clip's end. |
| Skeleton (`SkeletonScene.cpp:145`) | Meshes, bones and names on or off. |
| Skin diagnostics (`SkinDiagnostics.cpp:127`) | A static "see the log" line. |
| Blending (`Blending.cpp:261-272`) | Crossfade, upper-body weight and lean. |
| Bone attachments (`BoneAttachments.cpp:464`) | Clip, rifle, hat, timing and boxes. |
| Crowd (`Crowd.cpp:203-217`) | Count and bones, frame, palette and draws, and compute skinning. |
| Going limp (`GoingLimp.cpp:219`) | The arm override. |
| Playback (C#) (`scripts/Playback.cs:275,281`) | Two lines and a message. |
| Bone attachments (C#) (`scripts/BoneAttachments.cs:247`) | One line. |

World text labels stay. F2 does nothing in this app.

## PhysicsDemo (no ImGui)

- **F2:** `config.appKeyHelp` (`main.cpp:25`) cycles the physics debug lines (`AddDebugCategoryCycling`, `SceneLooks.cpp:139-181`). Smoke runs press it repeatedly.
- **Readouts:** `ShowReadouts` (`SceneLooks.cpp:118-137`; C# `scripts/Looks.cs:87-97`) draws each named entity's `Name` as screen text from (16,16), one every 20, in the corner where messages stack. Live readings are written into `Name`s first.
  - **Called from:** Character, Joints, Vehicles, Motorcycle and Tank (C++), and Character, Joints and Vehicles (C#).
- **Key help:** long lines for Character and Vehicles. The Vehicles help includes the gamepad.

## ScriptingDemo

World text only (`Showcase.cpp:142`). Its key help is one line: R, Space and G.

## Engine packages

- **No package depends on ImGui.**
- **Comments that name its vertex layout:**
  - `FenrirDraw2D/DrawList2D.hpp:44,321`
  - `FenrirRenderer/Shaders/Builtin/2d.vert:3`
  - `debug_text.vert:4`
  - `src/Core/BuiltinShaders.hpp:37`
  - `src/Core/DebugFeature.cpp:40`
  - `Tests/DrawLists2D.hpp:20`
  - `CMakeLists.txt:212`
- **What keeps `builtin:2d` and `Vertex2D` in use:**
  - the offscreen-group composite (`Draw2DFeature.cpp:539,729`)
  - RendererDemo's Overlay scene
  - the renderer, Draw2D and painter tests
- **`DebugFont`:**
  - It is JetBrains Mono in one stb_truetype SDF atlas, covering ASCII and Latin-1.
  - It is drawn by `DebugFeature` in the window's `Overlay` pass after the 2D feature, so it sits above every overlay layer, FenrirUI's included.
  - Its inputs are `DebugDraw::ScreenText` and `Message`.
