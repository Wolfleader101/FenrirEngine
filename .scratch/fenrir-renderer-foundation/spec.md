Status: ready-for-agent

# FenrirRenderer foundation: a renderer over OpenGL behind a private graphics device, with its window, input, assets and demo apps

Origin: the FenrirRenderer wayfinder map (`.scratch/fenrir-renderer/map.md`). This spec collapses the map's foundation tickets (08–23, 25 and 41) into one buildable plan. The hard-to-reverse calls are recorded in ADRs:

| ADR | Decision |
|---|---|
| 0016 | The graphics backend is picked at runtime behind a private graphics device |
| 0017 | FenrirInput owns input and FenrirWindow feeds it |
| 0018 | Shaders are GLSL compiled to SPIR-V at build time |
| 0019 | Each frame is a render graph built on the main thread |
| 0020 | FenrirWindow uses SDL3, and OpenGL stays on the main thread |
| 0021 | FenrirAssets owns plain-data assets behind generational handles |
| 0022 | FenrirWindow makes the graphics surface, and the renderer presents through it |
| 0023 | Each drawn mesh is one `MeshRenderer`, read directly each frame |
| 0024 | Materials are shared assets bound through a reflected material set (amended by ticket 41) |
| 0025 | FenrirDebugDraw owns debug shapes and text, and the renderer draws them |
| 0026 | Cameras draw with reversed-Z into windows or image assets |
| 0027 | A UI draws through FenrirDraw2D and takes input through input routers |
| 0029 | Scripting extensions carry native resources and deferred structural changes |

ADR-0031 also reaches the foundation, through its importer line: cgltf and ufbx replace assimp.

A throwaway spike (`prototype/fenrir-renderer-spike`, ticket 25) proved the seam, FenrirWindow and the ECS model end to end through real GL code. The animation spec (tickets 24 and 26–34: FenrirSkeleton, FenrirAnimation and skinning) builds on this one. So do the later feature families on the map.

## Problem Statement

FenrirEngine has no renderer. `packages/FenrirRenderer`, `packages/FenrirWindow` and `packages/FenrirInput` are empty placeholders. Every pixel the engine shows today comes from `examples/DemoApp`, which mixes all of the following in one app, written fast as a placeholder:
- GLFW, OpenGL calls, its own input structs and callbacks;
- meshes loaded with assimp into static buffers, and `.glsl` files compiled by the driver;
- a single-camera `FenrirCamera` and an ImGui editor;
- one `GL_LINES` pass for physics's debug lines.

None of it is a package. A game made with the engine cannot open a window, read a key, load a model, or draw a lit mesh through any of the engine's own interfaces, in C++ or in C#.

The engine developer wants a renderer stack that is the engine's own:
- its own components and systems, following the engine's ECS rules and FenrirPhysics's facade (`Install` returns an owner, plain-data components named by `Entity`, scene-scoped state as scene resources, C# through a scripting extension);
- public interfaces that never name the libraries underneath (GL and its loader, SDL3, cgltf, ufbx, stb, the shader compiler), as ADR-0010 asks;
- a graphics seam that lets D3D, Vulkan or Metal replace OpenGL later without touching code that calls the renderer;
- optional packages, so a headless app or server does not build SDL3 or GL;
- demo apps per feature area on a shared scaffold, replacing the DemoApp, which is deleted.

The engine and its packages also stand in the way. Each needs fixing first:
- FenrirApp has no app-wide resources: input devices, the asset store and the window's state have nowhere to live;
- there is no phase after `PostUpdate` that sees everything C# wrote and still runs before the event queues swap;
- nothing says which systems must run on the main thread, which SDL3 and GL require;
- a package's `Install` has no defined way to fail;
- C# cannot reach a resource, and an extension's native function cannot spawn entities (ADR-0009's ban);
- FenrirPhysics hands out packed debug lines that only the DemoApp can draw, and its debug-category enum is named `DebugDraw`, a name FenrirDebugDraw needs.

## Solution

Seven new packages and one build tool, each optional where it links a library:

- **FenrirInput:** device-neutral input types, app-wide `Keyboard`, `Mouse` and `Gamepads` resources, input events, input routers and input claims. It links no library and is always built.
- **FenrirWindow:** windows and graphics surfaces over SDL3, and the feed into FenrirInput.
- **FenrirAssets:** plain CPU-side asset data (meshes, images, materials, shaders, models) behind generational `AssetHandle<T>`s owned by the app or a scene. Models are imported with cgltf and ufbx, images with stb_image.
- **FenrirRenderer**, in three layers:
  - an ECS facade (`Renderer::Install`, `MeshRenderer`, `Camera`, `DirectionalLight`, `Sky`, `AmbientLight`, the `Overlay`);
  - a renderer core with no ECS dependency (draw list, render graph, render features);
  - a private graphics device with an OpenGL 4.6 backend and an always-built null backend, chosen at runtime.
- **`FenrirShaderCompiler`:** a build-time tool that compiles GLSL 460 into versioned shader containers holding SPIR-V, reflection and parameter defaults.
- **FenrirDebugDraw:** a `DebugDraw` scene resource of wire and solid shapes, screen text, on-screen messages and world text, with durations and groups. It has no GPU code and is built in every configuration.
- **FenrirDraw2D:** plain-data 2D draw lists that a UI library fills and the renderer draws on the window overlay.

Game code works the ECS way:
- **Drawing:** add a `MeshRenderer` (mesh, material, `visible`) to an entity, and a `Camera` to another. Every enabled camera in the active scene draws, in its `order`, into a viewport of the main window or of a render-target image.
- **Lighting:** one `DirectionalLight` component, plus the `AmbientLight` and `Sky` scene resources.
- **Materials:** shared `MaterialData` assets naming `builtin:unlit`, `builtin:lit` (metallic-roughness GGX) or a user's own GLSL program, with parameters set by name.
- **Debugging:** draw into the scene's `DebugDraw` from C++, C# or physics.
- **UI:** submit a `DrawList2D` to the overlay, and take input first through an input router.

Everything is available from C# through one scripting extension per package. Those extensions rest on a FenrirScripting prerequisite: native resources, the change buffer on the call context, and a `Fenrir.Math` contract.

The engine gains what these packages need, each as a neutral capability: app-wide resources, a `Render` phase, a main-thread rule, and one failure shape for `Install`. FenrirPhysics's debug output is rewritten to draw into FenrirDebugDraw.

Three demo apps (RendererDemo, ScriptingDemo, PhysicsDemo) on a shared demo scaffold replace `examples/DemoApp`, which is deleted with `FenrirCamera` and the ImGui editor.

## User Stories

### Drawing from C++

1. As a game developer, I want to install the window, input, assets and renderer with one call each, each returning an owner, so that turning rendering on is as simple as turning physics on.
2. As a game developer, I want a failed install (no GL 4.6 driver) to log why and leave the app running with that package inert, so that I can show my own message or fall back.
3. As a game developer, I want an entity with a `MeshRenderer` naming a mesh and a material to be drawn at its world transform, so that drawing needs no renderer call.
4. As a game developer, I want a mesh with several materials to be several child entities, so that each draw is plain data I can edit.
5. As a game developer, I want `visible = false` to hide one entity without removing anything, so that toggling is a field edit.
6. As a game developer, I want a missing material to draw flat magenta and a missing mesh to be skipped, each logged once, so that mistakes show on screen without crashing.
7. As a game developer, I want to load a glTF, GLB, FBX or OBJ model and spawn it as an entity tree with one call, so that imported scenes appear with their hierarchy and materials.
8. As a game developer, I want to build a mesh in code with the same handle type as a loaded one, and replace its data later, so that procedural geometry is drawn the same way.
9. As a game developer, I want the same path to load once and return the same handle, and assets owned by a scene to be freed with it, so that I do not track lifetimes by hand.
10. As a game developer, I want an unlit and a lit metallic-roughness material built in, so that most meshes need no shader of my own.
11. As a game developer, I want to write my own GLSL program on the engine's includes and name it from a material, so that I can draw anything the built-ins cannot.
12. As a game developer, I want to set a material's parameters by name, and to read the effective value, the override or the shader's default, so that tools and code agree on what is drawn.
13. As a game developer, I want shader authors to declare parameter defaults in the shader, checked at build time, so that a material only stores what it changes.
14. As a game developer, I want opaque, masked, alpha-blended and additive materials, and double-sided ones, so that glass, foliage and glows draw correctly.
15. As a game developer, I want one directional light and an ambient light that I can rotate and recolour at runtime, so that scenes are lit.
16. As a game developer, I want a `Camera` component oriented by its entity, perspective or orthographic, so that a camera is an entity I move like any other.
17. As a game developer, I want several cameras drawing at once, in an order, into viewports, so that split screen and picture in picture need no renderer code.
18. As a game developer, I want a camera to draw into an image that a material or the overlay samples, so that monitors, minimaps and scene views work.
19. As a game developer, I want a sky colour or cubemap per scene, overridable per camera, so that backgrounds are data.
20. As a game developer, I want to turn a screen point into a ray, a world point into a screen point, and find the camera under a point, so that picking and UI anchors work from `Update`.
21. As a game developer, I want an entity's world bounds and last frame's draw counts from the renderer, so that gameplay and tests can inspect what is drawn.
22. As a game developer, I want vsync off, on or adaptive, changeable at runtime, and MSAA set at install, so that I control latency and quality.
23. As a game developer, I want resizing and minimising the window to need no code, and a minimised app to sleep, so that the game behaves like a desktop app.
24. As a game developer, I want closing the main window to stop the app by default, and to be able to handle the close myself, so that "save before quitting?" is possible.

### Input from C++

25. As a game developer, I want to poll keys by key code or scancode, mouse buttons, position, motion and scroll, and gamepads, so that I can read input anywhere.
26. As a game developer, I want "just pressed" read in `Tick` to mean since the last tick, and elsewhere since the last frame, so that a tap is never lost or doubled at any frame rate.
27. As a game developer, I want every key, text, mouse and gamepad event through `EventReader<T>`, so that I see each press in order, once.
28. As a game developer, I want gamepads hot-plugged in the standard layout, so that controllers just work.
29. As a game developer, I want to hide or capture the cursor, set its shape, and read and write the clipboard, so that games and tools feel native.
30. As a game developer, I want input a UI consumed never to reach game state, and a UI taking focus to cancel keys the game holds, so that clicking a panel never fires a gun.

### Debug drawing

31. As a game developer, I want wire and solid shapes, arrows, grids, frustums and axes with a colour, thickness and depth mode, so that I can see rays, paths and volumes.
32. As a game developer, I want a shape drawn in `Tick` to stay until the next tick, and shapes to last a number of seconds or forever, so that nothing flickers or doubles.
33. As a game developer, I want named groups I can show, hide or clear, and shapes that follow an entity, so that debug views are organised.
34. As a game developer, I want screen text, keyed on-screen messages and world text that faces the camera, so that I can show live values without an editor.
35. As a game developer, I want debug drawing to work without a renderer and in release builds, so that tests and shipped builds can use it.
36. As a game developer, I want physics's debug lines to draw through the same debug drawing, so that one system shows everything.

### UI over the renderer

37. As a UI author, I want to submit 2D draw lists with my own shaders, vertex layouts, textures and scissors in logical coordinates, so that any UI library can draw over the game.
38. As a UI author, I want overlay layers drawn in order, with debug text always on top, so that UI never hides engine diagnostics.
39. As a UI author, I want linear blending by default, separate colour and alpha factors, and an explicit gamma path, so that UI blends correctly on any target.
40. As a UI author, I want an input router that sees events before the game and owns the controls it takes, so that a UI and the game share input cleanly.

### Using it from C#

41. As a script author, I want `MeshRenderer`, `Camera` and `DirectionalLight` writable from C#, so that C# systems build scenes exactly as C++ does.
42. As a script author, I want `Sky` and `AmbientLight` read and written as resources, so that scene settings are data in C# too.
43. As a script author, I want to load assets by path, make procedural meshes, images and render targets, and create, clone and edit materials by name, so that C# needs no C++ for content.
44. As a script author, I want `SpawnModel` from C# to return the root at once, so that I can spawn a model and position it in one system.
45. As a script author, I want the renderer's picking helpers, bounds and stats, so that C# can pick and inspect.
46. As a script author, I want `Keyboard`, `Mouse` and `Gamepads` with the same phase-following edges as C++, and input events through `EventReader`, so that C# input behaves the same.
47. As a script author, I want window size, focus, cursor, clipboard and title, so that C# can manage the window.
48. As a script author, I want every debug-draw call from C#, so that scripts can draw what they compute.
49. As a script author, I want calls with no package installed, no window or a stale handle to do nothing rather than throw, so that scripts are robust.

### Engine-wide

50. As an engine developer, I want packages to keep app-wide state as app resources, so that input devices and the asset store have one home.
51. As an engine developer, I want a `Render` phase after `PostUpdate`, so that rendering sees interpolated poses and everything C# wrote, whatever the install order.
52. As an engine developer, I want systems that touch SDL or the GPU to run on the main thread by rule, so that SDL3's and GL's threading rules hold.
53. As an engine developer, I want every package's `Install` to fail the same way, so that apps handle failure once.
54. As an engine developer, I want C# extensions to mirror resources and record structural changes, so that every package exposes resources and spawning the same way.
55. As an engine maintainer, I want no GL, SDL, cgltf, ufbx, stb, glslang or SPIRV-Reflect type or include path reachable outside its package's private directories, so that replacing any of them rewrites one directory.
56. As an engine maintainer, I want the null backend tests to run on every machine and preset, and GL tests to read pixels back on CI's llvmpipe, so that rendering is tested without a GPU runner.
57. As an engine maintainer, I want rendering and assets to be optional at build time, so that a server build stays the engine core plus physics and scripting.

### Demo apps

58. As the engine developer, I want a demo scaffold with a free-fly camera, a scene list on PageUp/PageDown, a help panel and smoke-run switches, so that each feature area gets a demo app cheaply.
59. As the engine developer, I want RendererDemo scenes for meshes and materials, cameras and targets, sky, debug draw and the overlay, with C# twins of the first and fourth, so that I can check each feature by eye.
60. As the engine developer, I want the scripting showcase and every physics scene ported faithfully to ScriptingDemo and PhysicsDemo, so that nothing the DemoApp showed is lost.
61. As the engine developer, I want CI to smoke-run every demo app on the null backend everywhere and on llvmpipe on Linux, so that crashes and GL errors are caught.
62. As the engine developer, I want `examples/DemoApp`, `FenrirCamera` and the ImGui editor deleted once the demo apps cover them, so that no placeholder code remains.

## Implementation Decisions

### Engine prerequisites: FenrirApp and FenrirScheduler

- **App-wide resources** (spike lesson 5). `App` gains `GetResource<T>`, `TryGetResource<T>`, `AddResource<T>` and `RemoveResource<T>`, with the same shape as scene resources: one value per type, destroyed with the App. Package owners (`Window`, `Renderer`, `Assets`) hold the `App` and reach their state through it, so the owner values can move. The scripting prerequisite mirrors these resources (`MakeAppResource`).
- **A `Render` phase** (ADR-0019). `SchedulePriority` gains `Render`, run once per frame after `PostUpdate` and before the event queues swap. It sees `AfterTicks`' interpolated poses and everything C# wrote in `Update` and `PostUpdate`. Writing scene state in `Render` is unsupported (documented).
- **The current phase and the frame and tick counts** (ticket 31, added 2026-10-22 while building 02 and 16). `App::CurrentPhase()` names the running phase (empty outside one), and `Time::frameCount` and `Time::tickCount` number the frames and fixed steps begun. FenrirInput's phase-following edges and FenrirDebugDraw's duration-0 lifetimes stamp what they record with these counts, so they need no marker systems and do not depend on install order.
- **Main-thread rule** (spike lesson 6). Sequential systems run on the thread that runs the App, which is the main thread; parallel systems run on workers. Every system that touches SDL or the GPU is added with `AddSequentialSystem(s)`: FenrirWindow's pump, FenrirInput's dispatch (it calls routers that may touch SDL text input) and every `Render`-phase system. The scheduler gains no new rule; the rule is documented on `AddSystem`, on `AddSequentialSystem`, and in each package's README.
- **`Install` failure shape** (spike lesson 7). Every new package's `X::Install(App&, XConfig)` returns its move-only owner.
  - On failure it logs one diagnostic and returns an owner whose `IsValid()` is `false`. An invalid owner installs no systems and its calls do nothing, so the app keeps running. This matches `Scripting::Install`'s `IsRunning()`.
  - Failures: `Renderer::Install` when the backend cannot start (no GL 4.6 core context, with SDL's message), or without a valid `Window` for any backend but `Null`; `Window::Install` when SDL's video subsystem cannot start. `Assets`, `Input` and `DebugDraw` cannot fail.
  - Physics and Scripting keep their current shapes.

### FenrirMath

- **`Fenrir::Colour`** (ADR-0025): four linear floats, `Colour::FromSrgb(0xRRGGBBAA)`, and named constants. Debug draw, materials and lights take it; the renderer packs it for vertices.
- `Math::Ray` and `Math::AABB` exist already; the renderer's helpers return them.

### FenrirInput (ADR-0017, ADR-0027)

- **Package:** links no library and is always built, like FenrirTime. `Input::Install(App&)` works with no window; `Window::Install` installs it if it is not there yet.
- **Types:** `Key` (key code, follows the layout), `Scancode` (physical position), `KeyModifiers`, `MouseButton`, `GamepadButton` and `GamepadAxis` (standard layout).
- **Input feed:** a public API through which FenrirWindow, or a test, passes neutral input in. The events are key down and up (key, scancode, modifiers, repeat, window), text (codepoints, window), mouse button, move (logical position and motion), scroll (notches), focus gained and lost (window), and gamepad connect, disconnect, button and axis. Everything is processed in `PreUpdate`.
- **App resources:** `Keyboard`, `Mouse` and `Gamepads` give held, just-pressed and just-released.
  - **Edges follow the reading phase.** Calls from `PreTick`, `Tick` or `PostTick` answer with the edges since the last tick, cleared after each `PostTick`. Calls from any other phase answer with the edges since the last frame.
  - A tap that starts and ends between two ticks reads as just pressed and just released in the next tick, with held false.
- **Units:** one app-wide state, and every event names its window. Mouse position is in the focused window's logical coordinates (float, top-left), motion in the same units (raw while captured), scroll in notches.
- **Events:** every input is also an engine event read through `EventReader<T>` (ADR-0012).
- **Input routers** (ADR-0027) sit between the feed and the game-facing state, in `PreUpdate`:
  - **Order:** feed → routers → whatever passes updates the resources and becomes game events, tick edges included. Nothing a router consumes reaches game state or a game reader.
  - **Registration:** `AddInputRouter(priority, router)` returns an id, and `RemoveInputRouter(id)` removes it. Routers run highest priority first, ties in registration order. A router added during dispatch starts next frame; one removed stops at once.
  - **Dispatch:** each router gets `BeginDispatch` before the frame's first event. Events go in arrival order until one router returns `Consumed`.
  - **Ownership per control:** a key, mouse-button or gamepad-button press belongs to whoever took it, and its repeats and release go straight to that owner. Motion, scroll and text are not owned; each event is dispatched. While a router holds `pointerCapture` for a window, that window's pointer events reach it first.
  - **Input claims:** `pointer`, `pointerCapture`, `keyboardFocus` and `textInput` (with a caret rectangle) per window, plus an app-wide `gamepad` claim. Routers publish them. The `InputClaims` app resource holds the union per window. Claims never filter events.
  - **Cancellation:** raising `keyboardFocus` or `pointerCapture` cancels the game-owned controls of that device in that window with a `cancelled` release, and the later physical release goes to no one. Focus loss cancels every owned control in the window and drops its capture. A removed router's controls end silently and its claims clear.
  - IME composition is reserved, not built: a composition event, commit as the text event, the caret from the claim, and cancellation.
- Input mapping (actions, bindings) is out of scope.

### FenrirWindow (ADR-0020, ADR-0022, ADR-0027)

- **SDL3 3.4.x**, static, `PRIVATE`, named by no public header. Built subsystems: video (with clipboard), events, keyboard, mouse, text input and IME, joystick, gamepad, haptic, hidapi and sensor. Off: audio, GPU, 2D render, camera, dialogs, tray, process and `SDL_main` (the engine keeps its `main`: `SDL_MAIN_HANDLED`, `SDL_SetMainReady`).
- **`Window::Install(App&, WindowConfig)`** starts SDL and keeps its config, but creates no window.
  - With a renderer (any backend but `Null`), the window and its surface are made when `Renderer::Install` requests them.
  - Otherwise a plain window with no API flag is made in the first frame's `PreInit`.
- **`WindowConfig`:** title, logical size, `visible` (default `true`; a hidden window for GL tests), `stopOnMainWindowClose` (default `true`), `minimisedFrameSleep` (about 7 ms), `unfocusedFrameSleep` (default 0), and `textInputByDefault` (default `true`).
- **Surface functions** on `Window`, keyed by `WindowId`, with neutral types only (`void*` proc addresses, integer handles): request a surface (API, debug context, backbuffer description), get a proc address, make current, present, set the swap interval, and read the pixel size. Window's surface functions are statics taking `App&` (spike lesson 5).
- **One main window**, keyed by `WindowId`; the API is shaped for several.
- **Each frame:** pump SDL at the start of `PreUpdate` (sequential), feed FenrirInput, call `Input::Dispatch(app)`, and update each window's logical size, pixel size and content scale. Sleep at the end of `PreUpdate` when nothing can draw.
  - `Input::Dispatch` processes everything fed since its last call and is a no-op with nothing pending. FenrirInput's own `PreUpdate` system calls it too, so pumped input reaches the game in the same frame whichever package was installed first (settled 2026-10-22, ticket 03).
- **Services:**
  - cursor modes (normal, hidden, captured with relative motion);
  - `SetCursorShape` (arrow, text, the four resize shapes, hand, not-allowed, hidden);
  - clipboard text get and set, and `SetTitle`;
  - focus events, fed to FenrirInput for cancellation;
  - SDL text input on, with the caret rectangle, in each window where a router claims `textInput`, else following `textInputByDefault`.
- **Events:** resized, focus changed and close requested, each naming the window. With `stopOnMainWindowClose`, closing the main window calls `App::Stop`.
- **Order and exit:** Window installs before Renderer and is torn down after it. Destroying a window whose surface is still held is a diagnosed error. Windows are destroyed only at teardown.
- A window freezes while dragged or resized on Windows (accepted; live redraw is later).

### FenrirAssets (ADR-0021, as amended by ADR-0024, ADR-0026 and ADR-0031)

- **Package:** plain CPU-side data, no GPU code. It depends on FenrirScene (scene owners) and FenrirApp, never on the renderer, and physics never sees it. `Assets::Install(App&, AssetsConfig)` returns the owner of the store, which is an app resource.
- **Handles:** `AssetHandle<T>` is a plain, trivially copyable `{ index, generation }`. `T` is a short asset name: `Mesh`, `Image`, `Material`, `Shader` or `Model` (the animation spec adds `Skeleton` and `Clip`). A handle resolves to `MeshData`, `ImageData`, `MaterialData`, `ShaderData` or `Model`. This spelling replaces every `AssetHandle<ImageData>`, `AssetHandle<MaterialData>` and `AssetHandle<ShaderData>` in ADR-0024 and ADR-0027, and matches C#'s marker types.
- **Owners and lifetime:** each `Load` or `Add` names an owner, the app or a scene. An asset is freed when its last owner ends or on `Unload`, which bumps the slot's generation. Loading a path already loaded returns the same handle and adds the owner. A stale or invalid handle in a component is skipped and logged once per handle.
- **Loading:** synchronous and main-thread only. `Load<T>(path, owner)` returns a result with the handle or a diagnostic (path, reason, importer message), and loads `file#Label` sub-assets the same way. Paths resolve against `AssetsConfig`'s root (default `assets/` beside the executable); absolute paths are taken as given.
- **Revisions:** `Add<T>(data, owner)` gives an ordinary handle with no path and no deduplication. `Replace(handle, data)` swaps the data and bumps a revision that every user sees.
- **Data shapes:**
  - `MeshData`: optional streams (positions, normals, tangents, UV0, UV1, colours), 32-bit indices, submeshes (index range plus material slot), and local bounds computed on `Add`, `Replace` and import. It has no skin section yet (see "Room left for animation").
  - `ImageData`: width, height, format (`RGBA8`, `RGBA16F`, `RGBA32F`) and a colour space set by use. Pixels are optional: an image with none is a render target (size, RGBA8 sRGB; ADR-0026). A cubemap is six faces of one size, for `Sky`. Mips are the renderer's.
  - `MaterialData`: the renderer's material (see "Materials and lights").
  - `ShaderData`: a shader container's bytes, opaque to FenrirAssets apart from its parameter table (see "Shaders").
  - `Model`: every source node (source name, source identity, rest TRS, parent index), pointing at its meshes and materials by sub-asset handle.
- **Importers:** behind a private compile-time seam, as ADR-0010 does. cgltf reads glTF and GLB, ufbx reads FBX and OBJ, and stb_image reads PNG, JPEG and HDR, all `PRIVATE` (ADR-0031 replaces assimp). The importer:
  - normalises quaternions and generates missing normals;
  - maps materials onto the built-ins (see "Materials and lights");
  - sets image colour spaces by first use;
  - substitutes a 1×1 white image with a warning when a material's texture fails.
- **Static content only in the foundation.** The importer reads meshes, materials, images and the node tree. A mesh that is skinned in its file is left out with one diagnostic naming it, as ADR-0031's "no half-built skinning" rule requires. The file's skeletons and clips are not imported. Skin data, `SkeletonData`, `ClipData`, bone classification and `SpawnSkeletons` arrive with the animation spec.
- **`SpawnNodes(scene, AssetHandle<Model>, parent = {})`** (ADR-0028, ADR-0031) is in FenrirAssets' facade. It spawns one entity per node with `Name`, `Transform` and its parent link, and returns `SpawnedNodes { Entity root; std::vector<Entity> nodes; }`, indexed by model node. In the foundation every node is an ordinary node; the animation spec makes it leave bones out.
- **Built-in assets:** FenrirRenderer registers its embedded shader containers with `Add` under `builtin:` names at install (`builtin:unlit`, `builtin:lit`, `builtin:2d`, and the fallback).
- **Material edits** live on the store, so C++ and C# match (ADR-0024, ticket 41). `name` is a `std::string_view` and `m` an `AssetHandle<Material>`:
  - Create and copy:
    - `AssetHandle<Material> CreateMaterial(AssetHandle<Shader>, AssetOwner)`
    - `AssetHandle<Material> CloneMaterial(m, AssetOwner)`, which copies overrides, not resolved defaults.
  - Set (each bumps the revision):
    - `SetFloat(m, name, float)`, `SetVector2/3/4(m, name, Math::VecN)`, `SetColour(m, name, Colour)`;
    - `SetTexture(m, name, AssetHandle<Image>, SamplerDescription)`;
    - `SetBlendMode(m, BlendMode)` with `SetAlphaCutoff(m, float)`, and `SetDoubleSided(m, bool)`.
  - Read:
    - `std::optional<V> TryGetFloat/Vector2/3/4/Colour/Texture(m, name)` returns the effective value (the override, else the shader's default). It is empty only for a name the shader lacks or a wrong type.
    - `std::optional<V> TryGetOverride…(m, name)` returns only an override.
    - `bool IsSet(m, name)` says whether an override exists.
  - `bool Reset(m, name)` removes an override. It bumps the revision only when it removed something, and reads never bump it.
  - An unknown name or wrong type on a set is logged once per material and ignored.
  - `std::span<const ShaderParameter> GetShaderParameters(AssetHandle<Shader>)` reads `{ name, type, default }` from the container alone, for an editor.

### Shaders and `FenrirShaderCompiler` (ADR-0018, spike lessons 1–2, ticket 41)

- **One language:** GLSL `#version 460` in the Vulkan dialect for every shader (engine, user, 2D). Rules:
  - explicit `layout(set, binding)` on every resource, and `layout(location)` on every input and output;
  - uniforms in blocks only;
  - [0, 1] depth and an upper-left origin;
  - draws indexed by `gl_BaseInstance`; `gl_InstanceIndex` is refused (GL has no equivalent).
- **`FenrirShaderCompiler`:** our own executable under `packages/FenrirRenderer/Tools/ShaderCompiler/`, linking glslang and SPIRV-Reflect, never linked into the engine. `fenrir_add_shaders(<target> ...)` runs it through `add_custom_command` with depfiles, for the engine's shaders and users' alike, naming each program's variant keys. Per program it:
  1. resolves `#include` (`GL_GOOGLE_include_directive`) against the engine's shared include directory;
  2. builds every combination of the named `#define` variant keys;
  3. compiles each stage as Vulkan GLSL to SPIR-V 1.0;
  4. writes the GL copy: sets zeroed, each binding flattened per resource kind as `set × 4 + binding` (uniform buffers, storage buffers and textures counted separately, so at most 4 bindings of a kind per set), and `VertexIndex` retargeted to `VertexId`;
  5. reflects and checks the parameter defaults;
  6. writes one shader container.
- **The flattening formula** is shared by the tool and the GL backend, and documented once.
- **Shader container** (versioned, one per program). It holds a header (magic, format version, program name), then per variant:
  - its key set;
  - per stage: the stage and its GL SPIR-V (the unflattened SPIR-V is kept for a later Vulkan backend);
  - reflection: vertex inputs; per set, each binding with its kind and name; the `MaterialParams` block in set 2 (set 1 for 2D shaders), each member with name, type, std140 offset and size; and texture bindings with names;
  - the parameter defaults;
  - the default block image.
- **Parameter defaults:** `#pragma fenrir default(<name>, <value>, ...)`, written beside the block or the sampler.
  - **Values:** `float`, `vec2`, `vec3` and `vec4` members take one to four float literals, matching the component count. A colour is a `vec4`. A sampler takes `white`, `black` or `normal`.
  - A member with no pragma is zero, and a sampler white, without a warning.
  - **Read per variant** from the preprocessed source, so a pragma inside an `#ifdef` applies only to the variants that define it.
  - **Build errors:** an unknown member; a malformed pragma; the wrong value count; two pragmas for one name in a stage; stages that disagree; a default on an array, struct or matrix member.
  - **The default block image** is the `MaterialParams` block packed with its defaults at the reflected std140 offsets and strides.
- **Engine includes:** `Fenrir/Frame.glsl` (set 0: time, the directional light, ambient), `View.glsl` (set 1: camera matrices, viewport), `Draw.glsl` (set 3: a storage buffer of world and normal matrices at binding 0), `Vertex.glsl` (standard locations: position, normal, tangent, UV0, UV1, colour) and `2D.glsl` (set 0 of 2D shaders: `localToClip`, pixel size, content scale).
- **Built-ins:** `unlit`, `lit`, `2d`, the fallback, the background (sky and clear), debug lines and triangles, SDF text, and `Output`. Each is built by the same function and embedded in FenrirRenderer as a byte array, so it runs with no files beside it.
- **No hot reload** in the foundation, and no shaders built from strings at runtime.

### FenrirRenderer: layers and the graphics device (ADR-0016, ADR-0019, ADR-0022)

- **Three layers, three targets:**
  - The **ECS facade** (`FenrirRenderer`, public) holds the components, the `Render`-phase systems and `Install`. It is the only part that links FenrirWindow (`PRIVATE`).
  - The **renderer core** (its own CMake target, headers not installed) holds meshes, materials, the draw list, the render graph, render features and recording. It has no FenrirApp, FenrirECS, FenrirScene or FenrirWindow dependency, and is tested with the null backend and no `App`.
  - The **graphics device** is a private abstract interface, with backends in `src/Backend/OpenGL/` and `src/Backend/Null/`. Only the OpenGL directory includes GL or its loader.
- **`GraphicsSurface`:** a private abstract interface declared by the core and used by the backends. The facade adapts the main window's surface to it, so core tests use a fake surface.
- **Device shape** is the explicit APIs': buffers, textures, samplers, pipelines, binding-group layouts and binding groups, and passes.
  - Resources are created and destroyed through virtual calls.
  - Commands are recorded through per-pass encoders into a plain-struct `CommandList` the renderer owns, and replayed by one virtual `Submit`. Recording needs no backend.
  - Handles (`BufferHandle`, `TextureHandle`, `PipelineHandle`…) are typed index-plus-generation values owned by the device. Destruction is deferred by frame index.
  - Barriers are derived inside each backend from declared pass usage; the GL backend derives none.
- **OpenGL backend:** a 4.6 core context. It uses DSA, immutable storage, samplers, `glBindBufferRange`, `glBindTextureUnit`, clip control and SPIR-V ingestion (`glSpecializeShader`). One linked program per vertex and fragment pair.
  - **Clip origin per pass** (spike lesson 3): upper-left for offscreen targets, so they are stored top row first like uploaded images; lower-left for passes drawing to the window. Front faces stay counter-clockwise in both.
  - **Validation:** with `RendererConfig::debugValidation`, the backend asks for a `KHR_debug` context with a synchronous callback, and messages go to FenrirLogger.
  - **Loader:** glad 2 (GL 4.6 core, `--mx`), checked in.
- **Null backend:** always built. It needs no window or context, creates handles, does all bookkeeping and draws nothing. It accepts any shader container.
- **`RendererConfig`:**
  - `backend` (`OpenGL` default, or `Null`); a backend that cannot start fails `Install` and never falls back to null;
  - `vsync` (`Off`, `On`, `Adaptive`), changeable at runtime through `Renderer`; an unsupported mode falls back to `On` and is logged;
  - `msaa` sample count for the scene targets (default 1, so off);
  - `debugValidation` (default on in Debug builds);
  - `framesInFlight` (default 2).

  No tuning settings and no native escape hatch.
- **Surface and present:** `Renderer::Install` requests the main window's surface: the API from `backend`, a 4.6 core context (debug when validation is on), and an sRGB colour backbuffer with no depth, stencil or MSAA. Present is the last step of `Render`, once per window drawn.
  - Each `Render` starts by reading the pixel size. A minimised or zero-sized window builds, submits and presents nothing, but the frame index still advances.
- **Teardown:** the `Renderer` owner waits for the GPU, frees device resources and releases the surface, before `Window` tears down.

### FenrirRenderer: the frame (ADR-0019)

- **`Render` phase, main thread,** all sequential:
  1. read the window sizes;
  2. read the active scene into the draw list and views;
  3. gather the asset data to prepare;
  4. prepare: upload what is new or changed, and create pipelines lazily;
  5. build and compile the render graph;
  6. record and `Submit`;
  7. present.
- **Prepare step:** the facade hands the core `(key, revision, data)` for each referenced asset that is new or changed. The core keeps GPU resources keyed by asset handle and revision, makes mips at upload, and frees the GPU side when a handle goes stale. The core uses FenrirAssets' data types, never its store.
- **Draw list:** per item a mesh, a material, a world matrix and a 64-bit sort key.
  - The stage is in the high bits.
  - Opaque items then sort by pipeline, material and mesh.
  - Transparent items sort back to front by the view-space depth of the item's world origin.
  - Overlay items keep submission order.

  Each item is one draw: no instancing, merging or culling.
- **Render graph, compiled every frame:**
  - Each pass declares its graph resources with precise usage (colour attachment, depth read or write, sampled, storage, copy source or destination), a queue kind (`Graphics`, `Compute`, `Copy`) and a record function into its own `CommandList`.
  - Order is by render stage (`Shadow, Opaque, Background, Transparent, Debug, PostProcess, Output, Overlay`), then registration order.
  - Passes whose outputs nothing reads are culled. A pass reading what nothing wrote is an error.
  - Transients come from a pool keyed by description, with computed lifetimes. External resources (the backbuffer, a camera's image target) are imported.
  - Per-frame buffers ring by frame index.
  - Render features (materials, background, debug, 2D) live in the core and add passes each frame. Game code and other packages add none.
- **Foundation passes per view:** `Opaque` (depth write) → `Background` (sky or clear at depth 0, after opaque) → `Transparent` (depth tested, no write, sorted; no OIT, documented) → `Debug` → `Output`. `Overlay` runs once per window, after every view's `Output`. One graph holds every view, and a view whose image another pass samples is ordered before it by dependency.
- **Targets:** each view draws into transient linear RGBA16F `SceneColor` and 32-bit float `SceneDepth`, at its viewport's pixel size and with `msaa` samples. `Output` resolves, clamps and sRGB-encodes into the view's rectangle of its target. It is the placeholder tonemap that post-processing replaces. `Overlay` draws on the backbuffer.
- **Designed for, not built** (listed in ADR-0019): memory aliasing, async compute and copy queues, parallel recording, split barriers, persistent pipeline caches, a render thread (the draw list is its snapshot), and a depth prepass for transparent items.

### FenrirRenderer: the ECS facade (ADR-0023)

- **`Renderer::Install(App&, RendererConfig)`** returns the move-only `Renderer` owner of the core and device, and installs the sequential `Render` systems. FenrirAssets and FenrirWindow (unless `Null`) must be installed first. Destroy it before the App. It has nothing to do with FenrirPhysics: physics-posed entities draw because they carry a `MeshRenderer`.
- **`MeshRenderer { AssetHandle<Mesh> mesh; AssetHandle<Material> material; bool visible = true; }`:** plain and blittable, one mesh, one material, one draw.
  - A mesh with several submeshes or materials is several child entities.
  - `visible` hides only its own entity.
  - An empty or stale mesh is skipped (a stale one logged once per handle).
  - An empty or stale material draws with the **fallback material** (flat magenta, unlit), logged once.
  - A missing `Transform` is the identity.
- **Read each `Render`:** the active scene's `MeshRenderer`s, with `GetWorldMatrix` once per item. Inactive scenes are never read, and nothing is kept between frames. The renderer has no scene resource of its own.
- **Read-back:**
  - `Renderer::GetWorldBounds(Entity) -> std::optional<Math::AABB>`, computed on call from `MeshData`'s local bounds and the world matrix;
  - `Renderer::Stats()`: last frame's draw, pass and upload counts.
- **`SpawnModel(scene, AssetHandle<Model>, parent = {}) -> Entity`** calls `SpawnNodes`, then adds a `MeshRenderer` per mesh of each node (a child entity per extra mesh or submesh). It returns the root. The animation spec inserts `SpawnSkeletons` and skin references between those two steps.

### Materials and lights (ADR-0024, ticket 41)

- **`MaterialData`:** a shader (`AssetHandle<Shader>`), a `BlendMode` (`Opaque`, `Masked` with `alphaCutoff`, `AlphaBlend`, `Additive`), `doubleSided`, and a name → value table of **overrides only**. Values are `float`, `vec2`, `vec3`, `vec4`, colour, or a texture (`AssetHandle<Image>` plus a `SamplerDescription`).
  - Shared by handle, with no per-entity overrides: a variation is another material.
  - Unset parameters read the shader's default, so they follow a changed shader.
  - An override the shader lacks is kept, logged once and ignored.
- **Binding sets:** 0 frame, 1 view, 2 material (`MaterialParams` at binding 0, then textures), 3 draw (matrices at binding 0). Set 2's layout and name map are built from the container's reflection. Sets 0, 1 and 3 are declared in C++ and checked against the reflection in debug builds (a mismatch fails pipeline creation with a diagnostic).
- **Render state:**
  - `Opaque` and `Masked` draw in `Opaque` with depth write; `AlphaBlend` and `Additive` draw in `Transparent` without it.
  - Depth test is always on: greater-or-equal, reversed-Z.
  - `doubleSided` turns culling off.
- **Built-ins:**
  - `builtin:unlit` is base colour × base-colour texture × vertex colour.
  - `builtin:lit` is metallic-roughness GGX with one directional light and constant ambient, writing linear HDR.
  - Parameters: `baseColour`, `baseColourTexture`, `metallic`, `roughness`, `metallicRoughnessTexture`, `normalTexture`, `emissive` and `emissiveTexture`.
  - Defaults by pragma: base colour white, emissive black, metallic 0, roughness 0.5, and the textures' `white`/`normal`/`black`. Nothing is seeded in C++.
- **Default textures:** the renderer owns one 1×1 each of `white`, `black` and `normal` (0.5, 0.5, 1, linear), each sampled with the parameter's sampler.
- **Variants:** the renderer picks the keys, never the user. `ALPHA_MASK` comes from `Masked`; `SKINNED` is reserved (see "Room left for animation"). A missing texture binds its default instead of adding a key. A user shader's own keys stay off.
- **User shaders** are full vertex and fragment programs on the engine includes, with their own set 2, built by `fenrir_add_shaders` and loaded as `ShaderData`. Lighting is the shader's choice.
- **Import mapping:** an unlit file material goes to `builtin:unlit` and the rest to `builtin:lit`, with the standard names. Alpha mode becomes the blend mode, double-sidedness is kept, and occlusion is dropped. Every factor the file gives, or that format's own default, is written (so glTF keeps metallic 1, roughness 1). Colour space follows first use: base colour and emissive sRGB, the rest linear, a conflicting later use warned.
- **Samplers:** each texture parameter carries its sampler (wrap, filter, anisotropy; from the file, else repeat, trilinear, anisotropy 8). The core deduplicates samplers by description.
- **Pipelines:** keyed by program and variant, render state, vertex layout, target formats and MSAA. Created lazily in the prepare step and cached while the shader asset lives. A vertex attribute the shader reads but the mesh lacks binds a constant default (normal +Y, UV 0, colour white), logged once. Materials referenced at load may be prepared early to soften the first-frame hitch (spike lesson 8).
- **Lights:** `DirectionalLight { Colour colour; float intensity; }` shines along its entity's world −Z. The first per active scene is used, and more are warned once. `AmbientLight { Colour colour; float intensity; }` is a scene resource, dim grey by default. Intensities are plain multipliers.

### Cameras, render targets and sky (ADR-0026)

- **`Camera`:** plain and blittable, on an entity whose world matrix is the view (looking along local −Z, +Y up). Fields:
  - `projection`, a tagged union: `Perspective { verticalFov = 60° in radians, near = 0.1 }` with an infinite far plane, or `Orthographic { height (full, world units), near, far }`. The aspect always comes from the viewport's pixel size.
  - `enabled` (default `true`) and `order` (default 0).
  - `viewport`: a normalised `{ x, y, w, h }` of the target, top-left origin, default `{ 0, 0, 1, 1 }`.
  - `target`, a tagged union: `Window(WindowId)` (the main window by default) or `Image(AssetHandle<Image>)`.
  - `background`, a tagged union: `Scene` (default, the `Sky`) or `Colour(c)`.
  - `drawDebug` (default `true`).
- **Reversed-Z:** depth in [0, 1], near at 1, `SceneDepth` cleared to 0, tested greater-or-equal, `Background` at depth 0.
- **Which cameras draw:** every enabled `Camera` in the active scene makes one view per frame.
  - Views into one target draw by `order`, lowest first. Ties go by entity id, logged once.
  - Each view gets its own scene targets, and `Output` writes only its rectangle. There is no depth-only layering.
  - A window with no camera is cleared to black, its overlay still drawn, logged once.
  - Skipped, logged once: a camera targeting a window other than the main window, a viewport under one pixel, or a stale or empty target image.
  - A minimised window's cameras are skipped silently.
- **Image targets:** a pixel-less RGBA8 sRGB `ImageData`, made with `Add` and resized with `Replace`.
  - Materials and the overlay sample it like any image, and the graph orders its writer first.
  - A view whose own draws sample its target gets the default texture in those draws, logged once.
- **`Sky`:** a scene resource beside `AmbientLight`, a tagged union of `Colour(c)` or `Cubemap(AssetHandle<Image>)`. Dark grey when absent.
- **Picking,** on `Renderer`, computed on call from the camera's current world matrix and its target's current size, so it is usable in `Update`:
  - `ScreenPointToRay(Entity camera, Math::Vec2) -> std::optional<Math::Ray>`
  - `WorldToScreenPoint(Entity camera, Math::Vec3) -> std::optional<Math::Vec2>`
  - `CameraAt(WindowId, Math::Vec2) -> Entity`: the highest-order enabled camera whose viewport holds the point.

  Points are in logical window coordinates for windows and pixels for images. A stale entity, no `Camera`, a point outside the viewport, or a point behind the camera gives empty. There is no mesh picking.
- `FenrirCamera` is not used by anything new; it is deleted with the DemoApp.

### FenrirDebugDraw (ADR-0025, ADR-0026)

- **Package:** depends on FenrirMath, FenrirScene and FenrirApp only. It links no library and has no GPU code. It is built in every configuration with no compile-out or enable flag. Its `Install(App&)` adds the expiry and merge systems and returns its owner. The implementation ticket names the owner, because `DebugDraw` is the scene resource's name.
- **A `DebugDraw` scene resource** per scene, in that scene's world space, drawn into every view of the scene. With no renderer, items still record, expire and toggle.
- **Calls:**
  - **Wire:** line, ray, arrow, polyline (open or closed), point (screen-sized), box (AABB or oriented), sphere, capsule, cylinder, cone, circle, arc, plane, grid, axes and frustum. Round shapes take a segment count with a default.
  - **Solid,** with alpha (depth tested without write, unsorted): box, sphere, capsule and caller triangle lists.
  - **Every call** takes a `Colour`, a group, an optional duration and an optional `relativeTo` entity. Shapes also take a pixel thickness (default 1) and a depth mode (depth tested by default, or on top).
- **Lifetimes:**
  - Duration 0 (the default) lasts until the next tick when submitted from `PreTick`, `Tick` or `PostTick`, else until the end of this frame's `Render`.
  - A positive duration lasts that many seconds of frame time.
  - `DebugDraw::Forever` lasts until `Clear()` or `Clear(group)`.
  - There are no item handles.
  - `relativeTo` items are in the entity's local space, follow its world transform and die with it.
- **Groups:** named, defaulting to `Default`. Each can be shown, hidden (still recorded) or cleared, and carries no style.
- **Text:**
  - Screen text at a pixel position, and on-screen messages stacked newest first from the window's top-left. A key replaces the message under it.
  - Both draw once in the `Overlay` of each window showing a view of the scene, in logical coordinates scaled by the content scale, as a fixed top layer. Image targets get none.
  - World text sits at a world point, at a constant pixel size, facing the screen, never occluded and skipped behind the camera. It draws in each view's `Debug` stage unless the camera's `drawDebug` is off.
- **Threads:** parallel systems write per-thread buffers, merged in registration order when the batch ends. Sequential systems write directly.
- **Drawing:** FenrirDebugDraw tessellates live, shown items into plain batches (line and triangle vertices tagged with depth mode and thickness, plus text runs). The renderer's debug feature uploads them into per-frame ring buffers and expands lines to screen-space quads in the vertex shader.
- **Font:** one embedded OFL monospace TTF, rasterised at start-up into one SDF atlas with stb_truetype (`PRIVATE` to FenrirRenderer). It covers printable ASCII and Latin-1, with `?` for any other codepoint.

### FenrirPhysics: debug output through FenrirDebugDraw (ADR-0025)

- FenrirPhysics depends on FenrirDebugDraw. Its tick systems draw each world's debug output into the world's scene's `DebugDraw`, in a `Physics` group, with duration 0, using `Fenrir::Colour`.
- **The category enum is renamed** `PhysicsDebugCategories` in C++ and C#, freeing `DebugDraw` for FenrirDebugDraw's resource. `SetDebugDraw` keeps its name and takes it; flags such as `Joints` and `Vehicles` keep theirs. Demo code and tests follow the rename.
- `PhysicsWorld::DebugLines()`, the packed `DebugLine` and `DebugColour`'s packed colours are removed. Physics's debug tests assert on the scene's `DebugDraw` tessellated batches instead.
- This ticket is the physics map's "Debug output through FenrirDebugDraw" line, built here because PhysicsDemo needs it.

### FenrirDraw2D and the window overlay (ADR-0027)

- **FenrirDraw2D:** plain data with no GPU code. It depends on FenrirAssets and FenrirMath only, and is built with FenrirAssets.
- **`DrawList2D`:**
  - vertex bytes and index bytes;
  - a table of **2D pipeline descriptions**: shader (`AssetHandle<Shader>`), vertex layout (stride; per attribute location, format and offset), index type (16 or 32 bit), blend mode (`AlphaBlend`, `PremultipliedAlpha`, `Additive`, `Opaque`), input colour space (`Linear`, `sRGB`), and blend space (`Linear` default, `Gamma`). A description holds no renderer handle.
  - ordered commands: pipeline index, up to 4 texture slots (`AssetHandle<Image>` plus a sampler, linear clamp by default), an optional scissor, an index offset and count, a base vertex, and up to 128 parameter bytes. A command over a limit is skipped and logged once.
  - Coordinates are local and logical: top-left origin, Y down, the mouse's units.
- **Renderer's 2D feature:** copies the bytes into per-frame ring buffers, realises pipeline descriptions lazily, cached by value, and resolves texture slots as materials do. 2D shaders take set 0 from the target (`Fenrir/2D.glsl`) and set 1 per command (parameter block at binding 0, laid out from reflection, then textures).
- **The window overlay**, the only 2D target built:
  - `Overlay` is an app resource of the facade, with `Submit(WindowId, int layer, DrawList2D)`. Submissions are made any time before `Render` and cleared after it.
  - Layers draw lowest first, then in submission order, then in command order. Debug screen text is a fixed layer on top.
  - Orthographic, with `localToClip` from the window's logical size, and no depth or culling. Scissors are scaled by the content scale, rounded outwards and clamped.
  - A submission to a window that is not drawn is dropped and logged once.
- **`builtin:2d`:** textured and vertex-coloured, with the ImGui vertex layout (position `vec2`, UV `vec2`, colour RGBA8). It decodes sRGB inputs and premultiplies in linear when the blend mode does.
- **Colour:**
  - Blending is linear, and the target encodes on write.
  - Colour and alpha factors are set per blend mode as the ADR-0027 table gives.
  - `Gamma` is allowed only on an 8-bit sRGB target; anywhere else it is a logged error and the pipeline draws as `Linear`.

### Scripting: the FenrirScripting prerequisite (ADR-0029)

- **Native resources.** An extension registers `MakeSceneResource<T>("Fenrir.Renderer.Sky")` or `MakeAppResource<T>(…)`, with read-only variants. C# declares a blittable struct with `[NativeResource("…")]`, and the layout is checked at load (a mismatch fails the load).
  - `Resources.Get<T>()` returns a `Ref<T>` (or a `ReadOnlyRef<T>`) into engine storage, the active scene's for a scene resource. `Resources.TryGet<T>(out …)` covers absence; `Get` on an absent resource logs and returns an empty ref.
  - Inserting and removing a resource are deferred structural changes. Access works from any C# code.
- **Facades:** a resource with behaviour (`Keyboard`, `Mouse`, `Gamepads`, `DebugDraw`) is a static C# class of native functions, so C++'s rules (phase edges, tessellation) are never re-implemented.
- **The change buffer on the call context.** An extension's native function may create and destroy entities, add and remove components, and insert and remove resources. A created entity is usable at once, and the rest plays back in order when native code regains control. Direct structural changes stay forbidden.
- **`Fenrir.Math`:** a pure managed shared assembly in `packages/FenrirMath/Managed/Fenrir.Math` (namespace `Fenrir.Math`): `Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Matrix4x4`, `Colour`, `Ray` and `Bounds`, each laid out as its FenrirMath counterpart.
  - `Vector3` and `Quaternion` move there from `Fenrir.Scripting`; `Transform` stays.
  - The built-in extension shares it first.
  - Physics's contract moves onto it.
- The README's extension sections are rewritten for these. ADR-0009 is amended.

### Scripting: the extensions (ADR-0029, ticket 22, ticket 41)

- **One extension per package:** `Fenrir.Assets`, `Fenrir.Renderer`, `Fenrir.Window`, `Fenrir.Input` and `Fenrir.DebugDraw`.
  - Each contract lives in `packages/<Pkg>/Managed/Fenrir.<Pkg>/`, and its native functions in a static `<Pkg>Scripting` library, so no package links FenrirScripting.
  - Each builds only with `FENRIR_ENABLE_SCRIPTING` and its package's option.
  - The app adds `X::ScriptingExtension()` for each. Each is static and needs no installed package, so scripts always load.
- **Components,** all writable straight mirrors (C++ field order in PascalCase, C++ defaults on `new`, `new` equal to `{}`): `MeshRenderer`, `Camera` and `DirectionalLight`. `Camera`'s unions use the `[TaggedUnion]` generator: `CameraProjection { Perspective, Orthographic }`, `RenderTarget { Window, Image }` and `CameraBackground { Scene, Colour }`. `WindowId` is a plain struct in `Fenrir.Window`.
- **Resources:** `Sky` (its union through the generator) and `AmbientLight` are writable scene-resource mirrors. `InputClaims` is a read-only app-resource mirror.
- **`Fenrir.Assets`:**
  - `AssetHandle<T>` is a blittable `{ Index, Generation }` with a phantom `T` (`Mesh`, `Material`, `Image`, `Model`, `Shader`).
  - `Assets.Load<T>(path, owner = AssetOwner.Scene)` takes a path, `file#Label` or a `builtin:` name. A failure returns an invalid handle and logs.
  - `Unload` and `IsValid`.
  - `AddMesh(positions, normals, uvs, indices, owner)` and `ReplaceMesh` take spans and compute bounds.
  - `AddRenderTarget(width, height, owner)` and `ResizeRenderTarget`; `AddImage(width, height, rgba8, owner)` and `ReplaceImage`.
  - **Materials** mirror the C++ edits:
    - `Materials.Create`, `Clone`;
    - `SetFloat`, `SetVector2/3/4`, `SetColour`, `SetTexture`, `SetBlendMode`, `SetAlphaCutoff`, `SetDoubleSided`;
    - `bool TryGet…(…, out v)` for the effective value, `bool TryGetOverride…(…, out v)`, `IsSet` and `Reset`;
    - `Shaders.GetParameterCount(shader)` and `Shaders.GetParameter(shader, index, out ShaderParameter, Span<char> name)`.
- **`Fenrir.Renderer`:**
  - `ScreenPointToRay(camera, Vector2, out Ray)`, `WorldToScreenPoint(camera, Vector3, out Vector2)` and `CameraAt(WindowId, Vector2) -> Entity`;
  - `GetWorldBounds(entity, out Bounds)` and `Stats(out RendererStats)`;
  - `SpawnModel(model, parent = Entity.None) -> Entity`, recorded through the change buffer, whose root is usable at once.
- **`Fenrir.Window`:**
  - `Window.Main`; `Size`, `PixelSize`, `ContentScale`, `IsMinimised` and `IsFocused` by `WindowId`;
  - `CursorMode` (get and set) and `SetCursorShape`; `Clipboard` (get and set); `SetTitle`; `RequestClose`;
  - resized, focus and close-requested as native events.

  Text getters fill a caller's `Span<char>` and return the length.
- **`Fenrir.Input`:**
  - mirrored enums `Key`, `Scancode`, `KeyModifiers`, `MouseButton`, `GamepadButton` and `GamepadAxis`;
  - facades with C++'s phase-following edges: `Keyboard` (`IsDown`, `JustPressed`, `JustReleased` by key or scancode, and `Modifiers`), `Mouse` (buttons, `Position`, `Motion`, `Scroll`) and `Gamepads` (`IsConnected`, buttons and `Axis` per pad);
  - every input event as a native event read through `EventReader<T>`.

  Routers stay C++.
- **`Fenrir.DebugDraw`:** a static `DebugDraw` facade with every C++ call (shapes, text, messages, durations including `Forever`, groups, `relativeTo`, depth mode, thickness), drawing into the active scene's `DebugDraw`. It is the only C# path to debug drawing; physics's contract keeps only `SetDebugDraw` with `PhysicsDebugCategories`.
- **No-op rules, physics's:** with the package absent, no window, no camera or a stale handle, functions do nothing and return `false`, zero, an invalid handle or `Entity.None`. A debug build warns once per native call site. Nothing throws.

### Build (ticket 21)

- **Libraries arrive as pinned git submodules under the package that links them,** static and `PRIVATE`, following Jolt's `libs/CMakeLists.txt`. Every override is local to that directory:
  - inherited directory `COMPILE_OPTIONS` cleared;
  - warnings-as-errors off and every target `SYSTEM`;
  - `BUILD_SHARED_LIBS OFF` and the dynamic MSVC runtime;
  - no LTO;
  - Debug and Release supported, while `RelWithDebInfo` and `MinSizeRel` are documented, not mapped.

  | Library | Submodule | Linked by |
  |---|---|---|
  | SDL3 (3.4.x tag) | `packages/FenrirWindow/libs/SDL` | FenrirWindow |
  | cgltf | `packages/FenrirAssets/libs/cgltf` | FenrirAssets |
  | ufbx | `packages/FenrirAssets/libs/ufbx` | FenrirAssets |
  | stb (`stb_image.h`, `stb_truetype.h`; a commit) | `packages/FenrirAssets/libs/stb` | FenrirAssets (image); FenrirRenderer (truetype, through its own `PRIVATE` include) |
  | glslang | `packages/FenrirRenderer/Tools/ShaderCompiler/libs/glslang` | `FenrirShaderCompiler` only |
  | SPIRV-Reflect | `packages/FenrirRenderer/Tools/ShaderCompiler/libs/SPIRV-Reflect` | `FenrirShaderCompiler` only |
  | imgui | `examples/DemoScaffold/libs/imgui` | the demo scaffold's `ImGuiOverlay` only |

  Each implementation ticket picks its exact tag.
  - **SDL3:** the subsystems ADR-0020 leaves off are off, with its tests, examples and install.
  - **glslang:** no HLSL front end, no optimizer, no binaries, tests or install.
  - **SPIRV-Reflect:** its static library only.
- **The GL loader** is glad 2 (GL 4.6 core, `--mx`), generated once by hand and checked in under `packages/FenrirRenderer/libs/glad/`, with a README holding the command. It is a static target named `FenrirGlad`, linked `PRIVATE` by the OpenGL backend only. No Python in the build.
- **The font:** one OFL monospace TTF, checked in under FenrirRenderer's resources with its licence, and embedded as a byte array.
- **Root options:**
  - `FENRIR_ENABLE_ASSETS` (`ON`) builds FenrirAssets, FenrirDraw2D, cgltf, ufbx and stb.
  - `FENRIR_ENABLE_RENDERER` (`ON`) builds FenrirWindow, FenrirRenderer, `FenrirShaderCompiler`, SDL3, glad and glslang. Renderer on with assets off is a configure `FATAL_ERROR`.
  - FenrirInput and FenrirDebugDraw are always built.
  - Package tests and scripting extensions follow their package's option, and extensions also need `FENRIR_ENABLE_SCRIPTING`.
  - With rendering off, no renderer, window or demo target exists.
- **Examples:** `FENRIR_BUILD_EXAMPLES` (`ON`) means the new demo apps. The legacy DemoApp moves behind `FENRIR_BUILD_LEGACY_DEMOAPP` (`OFF`), untouched apart from one guard: skip its own `add_subdirectory(assimp)` when an `assimp` target exists. It stays until it is deleted.
- **CI and README:**
  - The README's options table gains the three options.
  - CI's `FENRIR_CMAKE_OPTIONS` turns on assets, renderer and examples.
  - Linux CI installs the X11 and Wayland development packages SDL3's video needs.
- **`PublicHeaders.cpp`** per package fails the build if GL, SDL, cgltf, ufbx, stb or the shader compiler's libraries reach a public header, as physics does for Jolt.

### Demo apps (ticket 23)

- **Scaffold:** a static library `FenrirDemoScaffold` under `examples/DemoScaffold/`, a CMake function and a shared C# assembly.
  - `fenrir_add_demo_app(<name> SOURCES … SCRIPTS … ASSETS …)` makes the executable, links the scaffold, builds and copies the app's C# script project when scripting is on, copies assets, and registers the smoke tests.
  - The `Fenrir.Demo` assembly holds the `CSharpScene` marker and the scene-click event, and each app's scripts project references it.
- **`DemoScaffold::Install(App&, DemoConfig)`** installs Window, Input, Assets, Renderer, DebugDraw and, when built, Scripting. It adds:
  - a free-fly camera (right mouse to look, WASD, Shift, wheel) writing a camera entity's `Transform`;
  - the ordered scene list;
  - the smoke-run switches;
  - a scene-click event (a ray from `ScreenPointToRay`);
  - an F1 help panel in debug screen text (app, scene, the scene's keys);
  - an optional `ImGuiOverlay`, which is an input router and draw-list producer using `builtin:2d`, sRGB input and straight `AlphaBlend`, with its font atlas as an `ImageData`.

  A scene may spawn its own camera; the free-fly camera is then off until F3.
- **Keys:** PageDown/PageUp step scenes. F1 is help and F3 toggles the free-fly camera, both reserved by the scaffold. F2 is the app's. Every other key is the scene's.
- **Smoke runs:**
  - `FENRIR_DEMO_SCENE=<name>` and `FENRIR_DEMO_KEYS=1` keep their meaning.
  - `FENRIR_DEMO_SMOKE=<frames>` runs every scene (or the named one) that many frames with its keys pressed, and exits non-zero on any logged error or GL debug error.
  - `FENRIR_DEMO_BACKEND=null|opengl` picks the backend.
  - `FENRIR_DEMO_AUTOPLAY` goes.
  - CTest gets one test per app per backend.
- **RendererDemo scenes**, each added by the ticket that builds its feature, with its hand checks:
  1. *Meshes and materials:*
     - the imported backpack;
     - primitives in unlit and lit over a metallic/roughness grid;
     - the four blend modes side by side and a double-sided quad;
     - the magenta fallback and a stale or empty mesh;
     - keys rotate the light, change the ambient, toggle `visible` and edit a shared material's parameter.
  2. *Cameras and render targets:* split screen, an orthographic top-down minimap, and a monitor sampling a third camera's image. Keys cycle `order`, the layouts and the projection.
  3. *Sky:* colour, cubemap, and a camera overriding it with a colour.
  4. *Debug draw:* every shape wire and solid, depth tested and on top, each duration, groups, `relativeTo` an orbiting entity, world text, screen text and keyed messages.
  5. *Overlay:* a raw `DrawList2D` (layers, scissors, blend modes), and the ImGui overlay claiming input over the scene.
  6. *Meshes and materials (C#)* and *Debug draw (C#)*: twins of 1 and 4 on the scripting extensions.
- **ScriptingDemo:** one scene, "Showcase". It is the legacy scripting demo unchanged: `Spin`, `Bob`, Sparks, the C++→C# key event, the C#→C++ `Burst`, the C#-only `Expired`, hot reload, the `R` and `Space` keys and its README table, with its backpacks drawn by `MeshRenderer`.
- **PhysicsDemo scenes, in order:** Playground (C++) (the legacy Default's box stack, paddle and triangle-mesh backpack), Playground (C#), Joints (C++), Joints (C#), Character (C++), Character (C#), Vehicles (C++), Motorcycle (C++), Tank (C++), Vehicles (C#). F2 cycles the physics debug categories.
  - The port is faithful: the same bodies, keys, autopilots and log lines.
  - Only these change:
    - scene switching;
    - `DemoMesh` becomes `MeshRenderer`, and the trigger pad's tint swaps to a second shared material;
    - debug lines go through FenrirDebugDraw;
    - chase cameras become `Camera` entities;
    - the gamepad comes from `Gamepads`, replacing `DemoGamepad`;
    - the left-click raycast comes from the scene-click event.
  - The hand-check lists of physics tickets 29, 39 and 48 move into its README.
- **Assets:** shared assets live in `examples/assets/` (the backpack with its attribution, a cube, a sphere, the art-deco texture set, a skybox cubemap), and an app's own in `examples/<App>/assets/`. Each app's asset root is a folder in its build directory holding both. The legacy `.glsl` shaders are not carried over.
- **Deleting `examples/DemoApp`** is the last ticket, once the three apps cover every legacy scene and pass their smoke runs. With it go `FenrirCamera`, `examples/DemoApp/libs`, the ImGui editor (with ImGuizmo and v8), the `FENRIR_BUILD_LEGACY_DEMOAPP` option and the DemoApp's input structs. The README's "before opening a PR" smoke-run line is rewritten for the new apps.

### Room left for animation (ADR-0028, ADR-0031, ADR-0035)

The foundation builds none of this, but must not block it:
- **FenrirAssets** reserves `AssetHandle<Skeleton>` and `AssetHandle<Clip>`. `Model`'s node record keeps source identity and has room for a node kind (ordinary, synthetic owner, bone *i* of skeleton *k*). `MeshData` has room for a skin section (`u16x4` joints, `unorm16x4` weights, `boneCount`, `expectedSignature`, per-bone boxes).
- **Shaders:** `SKINNED` is a reserved variant key the renderer never sets yet. `Fenrir/Vertex.glsl` leaves the two locations after the standard attributes free for joints and weights. `Fenrir/Draw.glsl` leaves set 3 binding 1 free for the palette buffer, and the draw record leaves room for a per-item `paletteOffset`.
- **`SpawnModel`** is `SpawnNodes` followed by adding `MeshRenderer`s, so `SpawnSkeletons` and `SkinnedMesh` slot in between.
- **The renderer** takes on a FenrirSkeleton dependency only in the animation spec.

## Testing Decisions

- **What a good test is:** it drives a package through its public interface and asserts on observable results: draw lists, resource lifetimes, pixels, input state and events, asset data, debug batches, overlay submissions. It never asserts on private state. Every engine-level test runs the App one frame at a time with a chosen delta (`App::RunFrame`, the physics foundation's seam).
- **Seam 1 (new), the null backend through `RendererProbe`.** A friend struct in `tests/`, as `PhysicsProbe` is, that reads the `CommandList`s the null backend received and the device's live handles. It runs on every machine and preset. Cases:
  - which draws, in which pass and order, with which pipeline and bindings;
  - opaque sort keys and back-to-front transparent order;
  - the fallback material, and skipped stale or empty meshes;
  - variants (`ALPHA_MASK`); default textures; parameter defaults and overrides in the uploaded block;
  - several cameras in `order` with viewport rectangles, image targets ordered before their samplers, self-sampling replaced, and skipped cameras;
  - minimised windows still advancing the frame index;
  - deferred destruction and stale-handle detection; prepare re-uploading on a revision;
  - culled unused passes;
  - overlay layers, command order and resolved pixel scissors;
  - debug batches reaching the `Debug` and `Overlay` stages, and `drawDebug` off.

  The core is also tested alone with a fake `GraphicsSurface` and no `App`. Prior art: `PhysicsProbe` and the spike's null run (16 items, 37 draws, 9 passes).
- **Seam 2 (new), GL pixel probes.** Each test renders into a small offscreen target (about 64×64), reads it back and checks chosen pixels within a per-channel tolerance. A probe helper hides the read-back orientation. There are no golden images. Cases:
  - the clear colour and an unlit quad's colour;
  - depth order under reversed-Z;
  - a lit face against N·L with ambient;
  - masked cutoff and alpha blending;
  - an image target sampled by a material;
  - straight and premultiplied 2D quads over opaque and non-opaque destinations, layering, and scissors at a content scale other than 1.

  How the tests run:
  - GL tests run with debug validation; a GL error or high-severity message fails the test that caused it.
  - The context is made the app's way, through FenrirWindow with a hidden window, and never presented. On Linux it runs under `xvfb-run` if a display is needed.
  - Without a 4.6 context the tests report skipped (CTest `SKIP_RETURN_CODE`), never passed.
  - CI runs them on Linux on llvmpipe 4.6: the newest Ubuntu runner if its Mesa reports 4.6, else a newer-Mesa PPA. The job prints the version, and `MESA_GL_VERSION_OVERRIDE` is never used.
  - Windows CI runs null tests only, and developers run the GL suite on their own GPU before a PR.
- **Seam 3 (new), the input feed.** FenrirInput is tested headlessly by feeding the neutral events. Cases:
  - held, just-pressed and just-released per phase, including taps between ticks, frames with zero and several ticks, and repeats;
  - text, mouse units, gamepad hot plug, and events once per reader;
  - routers: priority and stable order, consumption, add and remove during dispatch, ownership of press, repeat and release, capture, cancellation on claim and on focus loss, removing an owning router, and two windows.

  FenrirWindow gets a few tests (create hidden, resize, close) that skip without a display.
- **Seam 4 (new), FenrirAssets' public interface,** headless. Cases:
  - handles, owners, deduplication by path, `Unload` and stale generations;
  - `Add`/`Replace` revisions and bounds;
  - load diagnostics and labels;
  - small glTF, GLB, FBX and OBJ fixtures giving the same node tree, meshes and material mapping;
  - a skinned fixture's mesh left out with its diagnostic;
  - image colour space by first use;
  - material overrides, `TryGet`/`TryGetOverride`/`IsSet`/`Reset`/`Clone`, and revisions bumped only on change;
  - `SpawnNodes`.
- **Seam 5 (new), `FenrirShaderCompiler` fixtures.** Cases:
  - the flattening formula;
  - `gl_InstanceIndex` refused;
  - includes and every variant combination;
  - reflection of set 2;
  - every pragma error, with a build failure, and the default block image at std140 offsets;
  - the container round-tripping through FenrirRenderer's parser.
- **Seam 6 (new), FenrirDebugDraw headless.** Cases:
  - tessellated batches for known shapes;
  - durations per phase (a tick-phase item across frames with no tick and with two), seconds, `Forever`, `Clear` and groups;
  - `relativeTo` following and dying;
  - per-thread merge order.

  Physics's debug tests move onto these batches.
- **Seam 7 (existing), FenrirScripting's harness.** Cases:
  - native resources: registration, layout checks, `Get`/`TryGet`, deferred insert and remove;
  - the change buffer from an extension;
  - `Fenrir.Math` layouts;
  - each extension's layouts and `new` equal to `{}`;
  - a C# system that loads a mesh, makes a material, sets a colour, and spawns a `MeshRenderer` and `Camera` that `RendererProbe` sees;
  - `SpawnModel` returning a usable root;
  - `ScreenPointToRay` agreeing with C++;
  - `Sky` written from C# reaching the draw list;
  - fed keys in `Keyboard.JustPressed` per phase and in an `EventReader`;
  - debug draw from C#;
  - material reads matching C++;
  - a missing package no-oping.

  Prior art: physics's extension tests and `ScriptingProbe`.
- **Engine prerequisites:** app resources die with the App; the `Render` phase runs after `PostUpdate` and before the swap; an invalid owner's systems do nothing.
- **Demo apps:** smoke runs in CTest on null everywhere and on llvmpipe on Linux. Whether a scene looks right is a hand check from each scene's list.

## Out of Scope

- A second graphics backend (the seam allows D3D, Vulkan or Metal; none is built), and macOS.
- Everything in the animation spec: FenrirSkeleton, FenrirAnimation, skeletons, clips, skin import, `SkinnedMesh` and GPU skinning (tickets 24, 26–34).
- More lights, shadows, image-based lighting and occlusion; post-processing, tonemapping and exposure; physical light units.
- Instancing, culling, a render thread, parallel recording; a depth prepass; order-independent transparency.
- Dynamic meshes with per-frame vertex updates (soft bodies), and particles.
- Per-entity material overrides and surface functions.
- Several windows; live redraw while dragging; drawing an inactive scene.
- Depth-only camera layering, render scale, culling layers, per-viewport screen text, HDR render targets.
- Inherited visibility.
- `LoadAsync`, a public loader registry, import options, KTX2 and compressed textures, a GPU-only asset flag; hot reload of shaders or art.
- Image and world-space 2D targets, sub-rectangle image updates, the `Painter` and UI framework, C#-authored draw lists, IME composition, rumble, touch and pen.
- `Mesh(AssetHandle<Mesh>)` in debug draw, loaded fonts, and durations in game time.
- Resources defined in C# by script assemblies, and `Res<T>` system parameters.
- Golden-image tests.
- Input mapping; the editor; 2D sprites, game UI, terrain, ray tracing, VR, audio; an asset database with cooking.
- Reviewing and cleaning up FenrirPhysics's code beyond its debug output.

## Further Notes

- **Decision history:**
  - the wayfinder map `.scratch/fenrir-renderer/map.md` and its foundation tickets (01–23, 25 and 41);
  - ADR-0016 to ADR-0027 and ADR-0029, and ADR-0031's importer line;
  - research branches `research/graphics-api-seams`, `research/opengl-target`, `research/glfw-or-sdl3`, `research/shader-languages`, `research/frame-building` and the others named in tickets 06 and 07;
  - the spike branch `prototype/fenrir-renderer-spike`.
- **Settled while writing this spec** (2026-10-22, by the user):
  - The foundation's importer is static only, with skinned meshes left out with a diagnostic until the animation spec.
  - Handles use short names (`AssetHandle<Image>`), replacing ADR-0024's and ADR-0027's `AssetHandle<ImageData>`/`<MaterialData>`/`<ShaderData>`.
  - `Install` fails by logging and returning an owner with `IsValid() == false`.
  - The physics debug rewrite is a ticket of this spec.
  - Physics's `DebugDraw` enum is renamed `PhysicsDebugCategories`.
- **Order for `/to-tickets`** (ticket 23):
  - The engine prerequisites, FenrirInput, FenrirWindow, FenrirAssets, the shader compiler and the device come first.
  - Then a lit mesh drawing.
  - Then the demo scaffold with an empty RendererDemo.
  - Each later feature ticket (materials and defaults, cameras and targets, sky, debug draw, overlay and routers, the scripting prerequisite and extensions) adds its RendererDemo scene and hand checks as part of done.
  - The physics debug rewrite precedes the PhysicsDemo port. ScriptingDemo and PhysicsDemo are ported near the end.
  - Deleting the DemoApp is last.
- **Risks:**
  - Unspiked: MSAA and its resolve, text and the SDF atlas, the overlay and routers, C#, MSVC and Linux builds, AMD, Intel and Mesa drivers.
  - CI's llvmpipe 4.6 depends on the runner's Mesa (26.2 confirmed; 26.0 and 26.1 unchecked).
  - SPIR-V on GL is less travelled; the fallback is SPIRV-Cross to GLSL inside the tool, with no change above the device.
  - The first frame hitches while pipelines are made (about 0.18 s in Debug on the spike).
- **Engine changes land outside the new packages:** FenrirApp and FenrirScheduler (app resources, `Render`, the main-thread rule), FenrirMath (`Colour`, `Fenrir.Math`), FenrirScripting (native resources, the change buffer) and FenrirPhysics (debug output). Several are independently useful and can be built first.
- **Vocabulary** follows `GLOSSARY.md`: graphics API, graphics backend, graphics device, graphics surface, main window, renderer core, draw list, render graph, render pass, render stage, render feature, view, shader container, shader variant, FenrirShaderCompiler, MeshRenderer, fallback material, material, material parameter, parameter default, parameter override, blend mode, material set, camera, viewport, render target, sky, FenrirDebugDraw, debug draw item, debug group, on-screen message, FenrirDraw2D, 2D draw list, 2D pipeline description, 2D target, overlay layer, FenrirAssets, asset, asset handle, asset owner, asset root, model, sub-asset, key code, scancode, input feed, input edge, input router, input claim, control owner, native resource, scripting extension, demo app, demo scaffold.
