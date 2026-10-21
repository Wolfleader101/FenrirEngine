# How are the demo apps split, and what does each show?

Type: grilling
Status: resolved
Blocked by: 14, 16

## Question

The single DemoApp is replaced by one demo app per feature area on a shared scaffold. How?

- The scaffold: what it holds (window, renderer, free-fly camera, scene switching, smoke-run switches like `FENRIR_DEMO_SCENE`), and whether it is a library under `examples/` or a CMake helper.
- The renderer demo app: what it shows (meshes, both materials, cameras and render targets, skybox, debug lines and text, C++ and C# scenes).
- The physics demo apps: one app or one per family; the joints, character and vehicle scenes rewritten on the new stack, C++ and C# alike; the hand-check lists they inherit.
- Where the ImGui demo editor sits among them, and when `examples/DemoApp` is deleted.

## Answer

No ADR (the split and the scaffold are cheap to reverse) and no new tickets: the foundation spec's own tickets carry it. Glossary: Demo app, Demo scaffold.

- **Apps:** three in the foundation, each one area with an ordered scene list: **RendererDemo**, **ScriptingDemo** and **PhysicsDemo** (every physics family as scenes of one app); **AnimationDemo** comes from ticket 34. An app is split further only when it needs to be.
- **Scaffold:** a static library `FenrirDemoScaffold` under `examples/DemoScaffold/`, a CMake function `fenrir_add_demo_app(<name> SOURCES … SCRIPTS … ASSETS …)` (executable, scaffold link, the app's C# script project built and copied when scripting is on, assets copied, smoke tests registered), and a shared C# assembly `Fenrir.Demo` (the `CSharpScene` marker, the scene-click event) that each app's scripts project references.
- **What the scaffold holds:** `DemoScaffold::Install(App&, DemoConfig)` installs Window, Input, Assets, Renderer, DebugDraw and, when built, Scripting; a free-fly camera (right mouse to look, WASD, Shift, wheel); the scene list; the smoke-run switches; a scene-click event (a ray from `ScreenPointToRay`, replacing the editor's `SceneClickEvent`); a help panel in debug screen text (app, scene and the scene's keys, instead of logging keys on entry); an optional `ImGuiOverlay` router (ticket 19) for apps' panels. A scene may spawn its own camera (the vehicles' chase camera); the free-fly camera is then off until toggled. Physics-only pieces (F2's debug-line modes, gamepad driving) stay in PhysicsDemo. `DemoGamepad` goes (FenrirInput's `Gamepads` from both languages), and `DemoMesh` goes (`MeshRenderer` and the built-in materials).
- **Keys:** PageDown/PageUp step to the next and previous scene; the scaffold reserves F1 (help panel) and F3 (free-fly camera on or off, back to the scene's camera); F2 is the app's (PhysicsDemo's debug-line modes); every other key is the scene's.
- **Smoke runs:** environment variables, as today. `FENRIR_DEMO_SCENE=<name>` and `FENRIR_DEMO_KEYS=1` keep their meaning in every app; new `FENRIR_DEMO_SMOKE=<frames>` runs every scene (or the one named) that many frames with its keys pressed and exits non-zero on any logged error or GL debug error; new `FENRIR_DEMO_BACKEND=null|opengl`. `FENRIR_DEMO_AUTOPLAY` goes (ScriptingDemo's scene keys cover it). `fenrir_add_demo_app` registers one CTest test per app per backend: null everywhere, opengl on Linux CI under llvmpipe (ticket 20).
- **RendererDemo scenes:**
  1. *Meshes and materials:* the imported backpack, primitives in `builtin:unlit` and `builtin:lit` over a metallic/roughness grid, the four blend modes side by side, a double-sided quad, the magenta missing-material fallback and a stale or empty mesh (ADR-0023's rules as hand checks); keys rotate the light, change the ambient, toggle `visible` and edit a shared material's parameter.
  2. *Cameras and render targets:* split screen in two viewports, an orthographic top-down minimap, a monitor mesh sampling a third camera's render-target image; keys cycle `order`, the viewport layouts and the projection.
  3. *Sky:* colour, cubemap, and a camera overriding it with a colour.
  4. *Debug draw:* every shape wire and solid, depth tested and on top, each duration, groups, `relativeTo` an orbiting entity, world text, screen text and keyed messages.
  5. *Overlay:* a raw `DrawList2D` on the window overlay (layers, scissors, blend modes) and the ImGui overlay claiming input over the scene.
  - C# twins of 1 and 4 ("Meshes and materials (C#)", "Debug draw (C#)") on the renderer scripting extension, waiting on ticket 22; no C# twins of 2, 3 and 5 in the foundation.
- **ScriptingDemo:** one scene, "Showcase": the legacy scripting demo unchanged (`Spin`, `Bob`, Sparks, the C++→C# key event, the C#→C++ `Burst`, the C#-only `Expired`, hot reload), same keys (`R`, `Space`) and README table, its backpacks drawn with `MeshRenderer`. The C# physics playground moves to PhysicsDemo.
- **PhysicsDemo scenes, in order:** Playground (C++) (the legacy Default's box stack, paddle and triangle-mesh backpack), Playground (C#), Joints (C++), Joints (C#), Character (C++), Character (C#), Vehicles (C++), Motorcycle (C++), Tank (C++), Vehicles (C#); ragdolls and soft bodies join later as scenes from the physics map's specs.
- **The physics rewrite is faithful:** same bodies, keys, autopilots and log lines, C++ and C# alike. Only these change: scene switching (PageUp/PageDown); `DemoMesh` to `MeshRenderer`, a recolour (the trigger pad's tint) swapping to a second shared material since ADR-0024 has no per-entity overrides; physics debug lines through FenrirDebugDraw; chase cameras as `Camera` entities; the gamepad from `Gamepads`; the left-click raycast from the scene-click event. The hand-check lists of physics tickets 29, 39 and 48 move into PhysicsDemo's README, updated only for these. Fixing demo behaviour is the physics map's clean-up.
- **Assets:** shared demo assets in `examples/assets/` (the backpack with its attribution, cube, sphere, the art-deco texture set, a skybox cubemap); an app's own in `examples/<App>/assets/`; the legacy `.glsl` shaders are not carried over. Each app's asset root is a folder in its build directory holding both.
- **The ImGui editor is not ported:** it is deleted with the legacy DemoApp, with ImGuizmo and v8; a future editor is its own app on its own map, free to use the scaffold. imgui joins as a submodule under `examples/DemoScaffold/libs` by ticket 21's rule.
- **Order in the foundation spec:** the scaffold and an empty RendererDemo come in one of the first tickets, once a lit mesh draws; each later foundation ticket (materials, cameras, sky, debug draw, overlay, scripting extension) adds its RendererDemo scene and hand checks as part of done; ScriptingDemo and PhysicsDemo are ported in their own tickets near the end.
- **Deleting `examples/DemoApp`:** the foundation spec's last ticket, once the three apps have a counterpart for every legacy scene and pass their smoke runs; with it go `FenrirCamera`, `examples/DemoApp/libs`, the `FENRIR_BUILD_LEGACY_DEMOAPP` option, and the README's "before opening a PR" smoke-run line, rewritten to name the new apps.

## Comments

- From ticket 34: skip and fallback diagnostics, the magenta missing-material fallback (ADR-0023) included, log as warnings, once each, and `FENRIR_DEMO_SMOKE` fails only on errors and GL debug errors, so RendererDemo's fallback hand checks smoke cleanly; errors are for failures of the engine's own contract (internal renderer failures, invalid GPU buffer access, failed shader compilation).
