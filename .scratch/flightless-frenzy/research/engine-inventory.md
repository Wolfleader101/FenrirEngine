# What FenrirEngine gives a game today

Research for [What does FenrirEngine give a game today?](../issues/01-what-fenrirengine-gives-a-game.md), read from the repo at commit `c1f63237` (branch `main` plus the Flightless Frenzy map commit). Facts with file pointers; no recommendations.

**How this was read.** Public headers, package READMEs, ADRs, the C# contracts and the demo sources were read directly. Nothing was built or run, so every statement is what the source and docs say, not observed behaviour. Paths are repo-relative. Line numbers are of this commit; lines in the long one-paragraph-per-line READMEs are paragraph lines.

**Short path names used below.**

| Short | Full |
| --- | --- |
| `R-README` | `packages/FenrirRenderer/README.md` |
| `Components.hpp` | `packages/FenrirRenderer/include/FenrirRenderer/Components.hpp` |
| `Renderer.hpp` | `packages/FenrirRenderer/include/FenrirRenderer/Renderer.hpp` |
| `AssetStore.hpp` | `packages/FenrirAssets/include/FenrirAssets/AssetStore.hpp` |
| `Phys/` | `packages/FenrirPhysics/include/FenrirPhysics/` |
| `PhysTests/` | `packages/FenrirPhysics/Tests/` |
| `UI-README` | `packages/FenrirUI/README.md` |
| `UIRoot.hpp` | `packages/FenrirUI/include/FenrirUI/UIRoot.hpp` |
| `SR` | `packages/FenrirScripting/README.md` |
| `R-map` | `.scratch/fenrir-renderer/map.md` |
| `P-map` | `.scratch/fenrir-physics/map.md` |

**Contradictions found between docs and code** (the code is what the sections below follow):

- `AssetStore.hpp:238-241` and `Model.hpp:111` say FBX and OBJ skinned meshes are left out; the ufbx importer reads skins and clips (`packages/FenrirAssets/src/Importers/Ufbx/UfbxModelImporter.cpp:1-2,175`) and AnimationDemo skins Kenney's FBX (`examples/AnimationDemo/README.md:24,29`).
- `packages/FenrirAnimation/README.md:5` says tick-time playback is a later ticket; the header and source have it (`packages/FenrirAnimation/include/FenrirAnimation/Animation.hpp:102-109`).
- `SR` around lines 31, 168 and 431 describe `Entity`/`Log`/`Time` only, deferred changes and `ctx.Query` as future; all exist in `packages/FenrirScripting/Managed/Fenrir.Scripting/Systems.cs` and `Entity.cs`.
- `docs/adr/0071-*.md:27` points at `.scratch/fenrir-ui-followups/` for planned UI controls; that directory is not in this commit (`.scratch/` holds `fenrir-physics`, `fenrir-physics-ragdolls`, `fenrir-renderer`, `flightless-frenzy`).
- `.scratch/engine-gaps/`, which the Flightless Frenzy map names for gap issues (`.scratch/flightless-frenzy/map.md:7,15`), does not exist yet.

---

## Running a game

### App, scheduler, time, fixed tick

- **`App`** (`packages/FenrirApp/include/FenrirApp/App.hpp:405`): constructed with a logger (`App(std::make_unique<ConsoleLogger>())`, :413). `Run()` runs the loop (:545); `RunFrame(deltaTime)` runs one frame from a given delta, reading no clock (:552); `Stop()` (:564); `IsRunning()` (:569).
- **Packages** are turned on with `X::Install(App&, XConfig)`, which returns a move-only owner kept alive while the app runs and destroyed before the `App` (App.hpp:397-403).
- **Systems** are `std::function<void(App&)>` (`packages/FenrirScheduler/include/FenrirScheduler/Scheduler.hpp:15`). `App::AddSystem` runs in parallel on worker threads; `AddSequentialSystem` runs on the main thread in the order added (App.hpp:459,471). There is no before/after/run-if ordering (Scheduler.hpp:13-14).
- **Phases** (`SchedulePriority`, Scheduler.hpp:27-54): `PreInit`, `Init`, `PostInit`, then per frame `PreUpdate`, `BeforeTicks`, (`PreTick`, `Tick`, `PostTick`) zero or more times, `AfterTicks`, `Update`, `PostUpdate`, `Render`, and `Exit`.
- **Time** (`packages/FenrirTime/include/FenrirTime/Time.hpp:9-52`): `deltaTime`, `tickRate = 1/60`, `accumulator`, `maxTicksPerFrame = 5` (extra owed time is dropped with a warning), `frameCount`, `tickCount`, `CurrentTime()`. Read through `App::GetTime()` (App.hpp:618), which returns a const reference.
- **Time scale and pause: not found.** `Time` has no scale field; grep for `timescale|time scale|pause` in `packages/FenrirTime/include` and `packages/FenrirApp/include` finds nothing.
- **Resources**: one value per type on the app (`App::AddResource/GetResource/TryGetResource/RemoveResource`, App.hpp:477-532) or on a scene (`Scene::AddResource/GetResource/RemoveResource`, `packages/FenrirScene/include/FenrirScene/Scene.hpp:30-64`).
- **Events**: `App::SendEvent<T>` (App.hpp:588) and `EventReader<T>::Read(app)` (App.hpp:716-731). Each reader sees each event once (ADR-0012, `docs/adr/0012-each-event-reader-sees-each-event-once.md`). Queues swap only after a tick has run, so tick readers miss nothing (App.hpp:653).
- **ECS** (`packages/FenrirECS/include/FenrirECS/`): `EntityList::CreateEntity/DestroyEntity/ForEach/View/Group` (`EntityList.hpp:40-147`), `Entity::AddComponent/GetComponent/HasComponent/RemoveComponent/AddChild/RemoveChild/ForEachChild` (`Entity.hpp:60-142`), `Transform` and `Name` (`DefaultComponents.hpp:12,45`), `GetWorldTransform/SetWorldTransform` (`WorldTransform.hpp:19,47`), `ComponentWatch` for add/remove tracking (`ComponentWatch.hpp`). EnTT is hidden behind it (ADR-0008).
- **Logger**: `ILogger` with `Log/Info/Warn/Error/Fatal` in `std::format` style, and `ConsoleLogger` (`packages/FenrirLogger/include/FenrirLogger/ILogger.hpp:20-32`, `ConsoleLogger.hpp`). `AddLogger` chains further sinks (ILogger.hpp:39). A file logger was not found in the public headers.

### Scenes and switching

- A **`Scene`** is a name, an `EntityList` and scene resources (Scene.hpp:13-80). Nothing else.
- `App::CreateScene(name)`, `ChangeActiveScene(name)`, `DestroyScene(name)` (refused for the active scene), `GetScene(name)`, `GetActiveScene()`, `ForEachScene` (App.hpp:620-645).
- Inactive scenes are kept but not simulated: their physics worlds do not step (`Phys/Physics.hpp:67-70`) and their pose pipelines pause (`packages/FenrirSkeleton/README.md:24`).
- **No transition, loading-screen or additive-scene facility** was found in App.hpp or Scene.hpp. The demo scaffold switches by destroying one scene and spawning the next from a C++ `spawn` function (`examples/DemoScaffold/README.md:48-50`).
- Assets loaded with a `Scene&` owner are freed when that scene is destroyed (`AssetStore.hpp:105-129`).

### Building a program outside `examples/`

- **Every engine package is a static library target** added by the root `CMakeLists.txt` with `add_subdirectory(packages/...)` (root `CMakeLists.txt:89-171`): `FenrirApp`, `FenrirECS`, `FenrirScene`, `FenrirScheduler`, `FenrirTime`, `FenrirLogger`, `FenrirMath`, `FenrirCore`, `FenrirInput`, `FenrirAssets`, `FenrirDraw2D` (INTERFACE), `FenrirSkeleton`, `FenrirAnimation`, `FenrirText`, `FenrirDebugDraw`, `FenrirWindow`, `FenrirRenderer`, `FenrirPainter`, `FenrirUI`, `FenrirPhysics`, `FenrirScripting`, and `Fenrir<Pkg>Scripting` per extension.
- **No install or export rules exist for the engine.** Grep for `install(`, `export(` and `find_package(` over every `CMakeLists.txt` and `.cmake` outside `libs/` finds them only in Iris (`packages/Iris/CMakeLists.txt:67-85`, `find_package(Iris)` consumer at `packages/Iris/Examples/Consumer/CMakeLists.txt:7`) and Catch2 lookup. So the only route today is a build that contains the engine's source tree (the game inside the repo, or the repo as a subdirectory of the game's build).
- **No README documents consuming the engine from another project.** `README.md` covers building the repo and its demos only (README.md:29-41, 72-133).
- **Feature flags** a consumer sets: `FENRIR_ENABLE_SCRIPTING` (OFF), `_PHYSICS`, `_ASSETS`, `_TEXT`, `_RENDERER`, `_UI` (ON), `FENRIR_BUILD_TESTS` (OFF), `FENRIR_BUILD_EXAMPLES` (ON), `FENRIR_FETCH_DEMO_ASSETS` (OFF) (root `CMakeLists.txt:11-19`; README.md:95-105).
- **Presets** (`CMakePresets.json`): `clang-x64-debug/release` (scripting and tests on), `msvc-x86-*`, `msvc-x64-*`, Linux presets. MSVC presets run through `scripts\msvc.ps1` (README.md:82-85). They are the engine repo's presets; a separate project has none.
- **Third-party code is git submodules** under `packages/*/libs/` and `libs/catch2` (README.md:74).
- **Platform facts**: OpenGL 4.6 only, plus a null backend (`packages/FenrirWindow/include/FenrirWindow/WindowTypes.hpp:76-80`; `Renderer.hpp:26-32`). Jolt needs AVX2 (README.md:98). CI builds `msvc-x64-release`; the Linux job is commented out (README.md:87). macOS is out of scope (`R-map:91-99`).
- **`fenrir_add_shaders`** is declared "for the engine's shaders and users' alike" (`cmake/FenrirShaders.cmake:1-2`). It passes `${FENRIR_SHADER_INCLUDE_DIR}` to the compiler (:65), and that is a normal (non-cache) variable set when the root `CMakeLists.txt` includes the file (:20). CMake functions read variables from the caller's scope, so a call from a directory above the engine's would not see it. Not tested; every existing caller is inside the engine's tree.
- **Variables that do cross directories**: the contract directories are `CACHE INTERNAL` (`packages/FenrirScripting/CMakeLists.txt:9`; `packages/FenrirScripting/cmake/FenrirScriptingExtension.cmake:101`), and they are placed under `${CMAKE_BINARY_DIR}`, the top-level build directory.
- **The demo scaffold is not engine code.** `FenrirDemoScaffold` and `fenrir_add_demo_app` live in `examples/DemoScaffold/`, are built only with `FENRIR_BUILD_EXAMPLES`, and have no install rule (`examples/DemoScaffold/README.md:3-5`; `examples/DemoScaffold/CMakeLists.txt:3`). ADR-0071 says lifting the demo UI kit into a package "is a later decision, taken only once a game asks for it" (`docs/adr/0071-*.md:11`).
- **What the scaffold does that a game would otherwise write**: installs Window, Input, Assets, DebugDraw, Renderer and UI; installs Scripting with every extension; keeps a scene list; provides a free-fly camera; copies asset folders beside the executable after each build; registers CTest smoke runs (`examples/DemoScaffold/README.md:9-50`; `examples/DemoScaffold/cmake/FenrirDemoApp.cmake:39-63`).
- **Minimum C++ without the scaffold**: the renderer README's sample installs Window, Assets and Renderer and adds a mesh, material, camera and light in about 15 lines (`R-README:5-28`). It calls a `MakeCube()` that is defined nowhere in the repo.

### The C# side of the build

- **.NET 10 SDK**; every csproj targets `net10.0` (README.md:31). No `global.json` in the repo.
- **The scripting SDK folder** `<build>/fenrir-scripting/contract/` holds `Fenrir.Scripting.dll`, `Fenrir.Math.dll`, the source generator and `Fenrir.Scripting.props`, built by target `FenrirScriptingContract` (`packages/FenrirScripting/CMakeLists.txt:9-47`).
- **A script project outside the repo is documented**: a csproj that imports `$(FenrirScriptingSdk)Fenrir.Scripting.props`, built with `dotnet build -p:FenrirScriptingSdk=<folder>/` (`SR:54-71`). Other contracts (`Fenrir.Input`, `Fenrir.Physics`, ...) are referenced as built DLLs from `<build>/fenrir-<pkg>/contract/` with `Private=false` (pattern in `examples/ScriptingDemo/scripts/ScriptingDemo.Scripts.csproj`). Razor needs `packages/FenrirUI/Managed/Fenrir.UI.Razor.props` as a second import (`SR:73`).
- **No engine-level CMake function builds a script project.** `fenrir_add_demo_scripts` is in the demo scaffold, always passes the `Fenrir.Demo` contract and demo Razor props, and requires `<dir>/<App>.Scripts.csproj` (`examples/DemoScaffold/cmake/FenrirDemoApp.cmake:97-159`).
- **`fenrir_deploy_scripting(<target> EXTENSIONS <Pkg>...)`** is engine code: it copies nethost, the Iris bridge, `Fenrir.Scripting.dll`, `Fenrir.Math.dll` and each named contract beside the executable (`packages/FenrirScripting/cmake/FenrirScriptingDeploy.cmake:7-55`).
- **The .NET runtime is not bundled.** `nethost` beside the executable finds an installed .NET 10 runtime, or `ScriptingConfig::dotnetRoot` names one (`packages/Iris/README.md:98`). No self-contained or AOT deployment was found.
- **No NuGet packaging** of any contract (grep for `.nupkg`, `IsPackable`, `GeneratePackageOnBuild`: nothing).
- **`FenrirScripting.sln`** (root) holds Fenrir.Scripting, its generators and fixture, Fenrir.Math, Input, Window, Assets, Renderer, DebugDraw, Demo, and the RendererDemo and ScriptingDemo scripts. It does not hold Physics, Skeleton, Animation, Text, Painter, UI or UI.Razor.

---

## Input

Package `FenrirInput` (always built; `packages/FenrirInput/README.md`), fed by `FenrirWindow` from SDL3 (ADR-0017).

- **Polled app resources** `Keyboard`, `Mouse`, `Gamepads` (`packages/FenrirInput/include/FenrirInput/Devices.hpp:78-208`): `IsDown`, `JustPressed`, `JustReleased` for keys by `Key` (layout) or `Scancode` (position), mouse buttons, and gamepad buttons per pad.
- **Edges follow the reading phase**: read in `PreTick/Tick/PostTick` they are edges since the last tick, elsewhere since the last frame, so a tap is neither lost nor doubled (`packages/FenrirInput/README.md`, "Polled state").
- **Mouse**: `Position()` in logical window coordinates, `Motion()` (raw while captured) and `Scroll()`, summed since the last tick or frame (Devices.hpp:145-154).
- **Cursor capture / relative look**: `Window::SetCursorMode(app, window, CursorMode::Normal | Hidden | Captured)`; `Captured` hides and holds the cursor and reports raw relative motion (`packages/FenrirWindow/include/FenrirWindow/WindowTypes.hpp:49-58`; `Window.hpp:96`).
- **Gamepad**: `Gamepads::Connected()`, `IsConnected`, `IsDown/JustPressed/JustReleased(pad, GamepadButton)`, `Axis(pad, GamepadAxis)` with sticks -1..1 and triggers 0..1 (Devices.hpp:171-191). Buttons use the standard layout named by position (`South`, `East`, ..., `InputTypes.hpp:147`); axes are `LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger` (`InputTypes.hpp:181-188`). Several pads are supported by `GamepadId`.
- **Events**: `KeyEvent`, `TextEvent`, `MouseButtonEvent`, `MouseMotionEvent`, `MouseScrollEvent`, `WindowFocusEvent`, `GamepadConnectionEvent`, `GamepadButtonEvent`, `GamepadAxisEvent` (`packages/FenrirInput/include/FenrirInput/InputEvents.hpp`).
- **Input routers** (ADR-0027): a UI sees input before the game and may consume it; the game reads `InputClaims` to know what is claimed (`packages/FenrirInput/include/FenrirInput/InputRouter.hpp:91-125`). FenrirUI registers one per root.
- **Action or binding layer: none.** "Input mapping (actions and bindings), rumble, IME composition, touch and pen are out of scope" (`packages/FenrirInput/README.md`, "Not here yet"). Stick dead zones: not found in FenrirInput (the only dead zone is FenrirUI's navigation one, `UI-README:42`).
- **Window** (`packages/FenrirWindow/include/FenrirWindow/Window.hpp:51-157`): `Install(app, WindowConfig{title, logicalSize 1280x720, visible, stopOnMainWindowClose, ...})`, `LogicalSize`, `PixelSize`, `ContentScale`, `IsMinimised`, `IsFocused`, `SetTitle`, `SetIcon`, cursor mode and shape, clipboard, `RequestClose`. Fullscreen, borderless, resizing from code and display-mode selection: not found (grep `fullscreen|borderless|resizable|SetSize` in `packages/FenrirWindow/include` and its README). One window only is drawn to (`Components.hpp:206-219`).
- **From C#** (`Fenrir.Input`, `Fenrir.Window`): `Keyboard`, `Mouse`, `Gamepads` static facades with the same edge rule, every input event through `EventReader<T>`, `InputClaims` read-only; `Window.Main/Size/PixelSize/ContentScale/IsFocused/GetCursorMode/SetCursorMode/SetCursorShape/SetTitle/GetClipboard/SetClipboard/RequestClose` (`packages/FenrirInput/Managed/Fenrir.Input/Generated/`; `packages/FenrirWindow/Managed/Fenrir.Window/Generated/Window.cs`). Routers stay C++.

---

## Rendering

Package `FenrirRenderer` (`R-README`); OpenGL 4.6 forward renderer with clustered lights, a depth prepass and a render graph on the main thread (ADR-0016, 0019, 0049).

### Meshes

- `MeshRenderer { mesh, material, visible, castShadows }`: one mesh, one material, one draw (`Components.hpp:24-36`).
- `InstancedMeshRenderer { mesh, material, instances, castShadows, visible }` draws every copy in an `InstanceList` asset (per-copy `Mat4` and optional colour) (`Components.hpp:51-60`; `packages/FenrirAssets/include/FenrirAssets/InstanceListData.hpp:19-25`). Matching opaque items are also instanced automatically (`Renderer.hpp:69-74`).
- `SkinnedMesh { Entity skeleton }` beside a `MeshRenderer` (`Components.hpp:62-81`).
- **Built-in primitive meshes: none.** Demos load `examples/assets/cube.obj` and `sphere.obj` (`examples/RendererDemo/src/MeshesAndMaterials.cpp:123-124`); the scaffold has `ScreenQuad` (demo code). Meshes can be made in code: `store.Add<Mesh>(MeshData)` (`packages/FenrirAssets/include/FenrirAssets/MeshData.hpp:116-151`).
- **Model to entities**: `SpawnModel(app, scene, AssetHandle<Model>, parent)` makes an entity per node, mesh renderers, skeletons and glTF punctual lights (`packages/FenrirRenderer/include/FenrirRenderer/SpawnModel.hpp:15-43`).

### Materials, transparency, unlit

- `MaterialData { shader, blendMode, alphaCutoff, doubleSided, overrides }`; a material stores only overrides of its shader's defaults (`packages/FenrirAssets/include/FenrirAssets/MaterialData.hpp:93-107`). Edited through the store: `CreateMaterial`, `CloneMaterial`, `SetFloat/Vector/Colour/Texture`, `SetBlendMode`, `SetAlphaCutoff`, `SetDoubleSided` (`AssetStore.hpp:420-500`).
- **Built-in material shaders**: `builtin:lit` (metallic-roughness: `baseColour`, `emissive`, `metallic`, `roughness`, `emissiveStrength`, and base colour, metallic-roughness, normal, emissive and occlusion textures; `packages/FenrirRenderer/Shaders/Builtin/lit.frag:18-50`), `builtin:unlit` (`unlit.frag`), `builtin:fallback` (magenta).
- **Blend modes**: `Opaque`, `Masked`, `AlphaBlend`, `Additive` (`MaterialData.hpp:18-28`). Transparent items are sorted back to front by item origin, depth-tested without depth write, cast no shadows, and take no GTAO or SSR; no per-triangle sorting or order-independent transparency (`R-README:95`; `Components.hpp:31-34`).
- **Render state a material can set**: blend mode and `doubleSided` only. No depth-test, depth-write, cull-front or stencil field exists on `MaterialData`.
- **Per-entity tint**: `MeshRenderer` has none. Per-copy colour exists on `InstanceList` only (`packages/FenrirRenderer/Shaders/Builtin/mesh.vert:37-38`). Per-entity material overrides are planned only (`R-map:87`); today a different colour is a cloned material.

### Custom shaders and materials

- **A game can write its own material shader; this is documented and used by the demos** (`R-README:99`; ADR-0024, `docs/adr/0024-*.md:20`):
  1. Write a full vertex + fragment program in GLSL `#version 460` on the engine includes.
  2. `fenrir_add_shaders(<target> PROGRAM <name> VERTEX <f> FRAGMENT <f> [KEYS ...] [EMBED])` compiles it at build time to a `.fshader` container, optionally embedded as `Fenrir::Shaders::<name>` (`cmake/FenrirShaders.cmake:1-17`).
  3. `store.Add<Shader>(ShaderData{bytes})` or `AddBuiltin<Shader>("builtin:<name>", data)`.
  4. `store.CreateMaterial(shader, owner)` and set parameters by name.
- **Parameters are reflected** from the shader's `MaterialParams` uniform block and its set-2 samplers, with defaults from `#pragma fenrir default(...)` (`packages/FenrirAssets/ShaderContainer/include/FenrirAssets/ShaderContainer.hpp:62-96`; example `examples/RendererDemo/shaders/stripes.frag:8-20`).
- **What a custom shader can read** (`packages/FenrirRenderer/Shaders/include/Fenrir/`): `Frame.glsl` (sun direction and colour, time, sky irradiance and specular, the light buffer, shadow atlases), `View.glsl` (view and projection matrices, camera position, cascades, light clusters, AO, last frame's colour), `Draw.glsl` (`CurrentDraw()`: world matrix, per-instance colour), `Vertex.glsl` (vertex streams). `Lighting.glsl` offers `FenrirLitColour(FenrirSurface)` for the full lit result (:291) or the pieces (`FenrirDirectLight` :112, `FenrirSkyIrradiance` :80); `Shadows.glsl` offers `FenrirSunShadow` (:115), `FenrirSpotLightShadow`, `FenrirPointLightShadow`.
- **Scene depth and scene normals are not bound to material shaders** (neither appears in `Frame.glsl` or `View.glsl`).
- **Constraints**: the vertex stage also runs in the depth prepass and shadow passes and must write the world normal; without a `SKINNED` key a skinned mesh draws as the fallback; a user shader's own keys stay off (`R-README:96,99,113,191`).
- **No shader loading by file path**: `Load<Shader>(path)` works for `builtin:` names only (`packages/FenrirAssets/src/AssetStore.cpp:432-436`). No runtime GLSL compilation; shader hot reload is planned only (`R-map:83`).
- **Examples**: `examples/RendererDemo/shaders/stripes.*` (its own non-PBR wrap lighting, wired at `examples/RendererDemo/CMakeLists.txt:22-23`, `src/MeshesAndMaterials.cpp:170-174`), `monitor.*`; `examples/LightingDemo/shaders/{grass,ground,facade,street,sign}.*` (all end in `FenrirLitColour`; `examples/LightingDemo/CMakeLists.txt:34-39`); `examples/AnimationDemo` `flat.*`.
- **Cel shading**: no toon shader exists in the repo. The inputs a banded-lighting material shader would read (sun direction, sun shadow, light buffer, sky irradiance) are in the public includes above, and `stripes.frag` is an existing custom-lit material.
- **Outlines**: no outline, stencil or depth-edge facility exists (grep `stencil|outline` in the renderer's public headers, device sources, shader includes and README finds only the backbuffer's "no depth, stencil or MSAA" and the debug font's text outline).
- **A game cannot add a post-process, full-screen or render-graph pass.** The public renderer headers are `Components.hpp`, `Renderer.hpp`, `SpawnModel.hpp`, `Overlay.hpp`, `ImageTargets2D.hpp`, `WorldTargets2D.hpp`, `LensDirt.hpp`. The render graph and device are private; "game code and other packages add none" (`packages/FenrirRenderer/src/Core/RenderFeature.hpp:24-27`); "no native escape hatch" (`Renderer.hpp:53`). A user compute pass in the `Cull` stage is a not-yet in ADR-0057 (`docs/adr/0057-*.md:40`).
- **What is reachable for screen-space work**: a second `Camera` drawing into an image target (tonemapped RGBA8 sRGB only; HDR targets planned, `R-map:89`), and `Overlay::Submit(window, layer, DrawList2D)` with a user 2D shader over the window, which gets no scene depth (`R-README:126-144`).
- **From C#**: custom shaders cannot be added from C#; C++ registers one with `AddBuiltin` and C# loads it by its `builtin:` name (`examples/RendererDemo/src/MeshesAndMaterials.cpp:345-349`).

### Lights, shadows, sky

- `DirectionalLight` (first one only), `PointLight`, `SpotLight`, `RectLight` (LTC, no shadows) (`Components.hpp:845-979`). Clustered 16x9x24 with at most 256 lights per cluster (`R-README:194`).
- **Shadows**: cascaded sun shadows per camera (1-4 cascades, 2048 maps), spot shadows from a budget of 8, point shadows from a budget of 4; only opaque and masked materials cast (`R-README:103-114`; `Renderer.hpp:116-149`).
- **Sky** is a scene resource: `ColourSky`, `CubemapSky` or `PanoramaSky`; it is both backdrop and image-based ambient light; `SkyLight` overrides the lighting sky (`Components.hpp:982-1153`; ADR-0047). No procedural sky, no light probes, no `AmbientLight` component (`R-README:244`).
- **Fog: not built**; planned only (`R-map:72`).

### Post-processing

All per camera, as fields of the `Camera` component (`Components.hpp:675-832`):

- `look`: exposure (manual), tonemapper (`None | AgX | PbrNeutral | Aces`), bloom (on by default), anamorphic streaks, lens flare, parametric colour grading, chromatic aberration, vignette, film grain (ADR-0059).
- `ambientOcclusion` (GTAO, off by default), `reflections` (SSR, off by default; ADR-0056), `debugView`.
- **Anti-aliasing**: MSAA only, `RendererConfig::msaa` 1/2/4/8 set at install (`Renderer.hpp:60-64`). TAA, FXAA, SMAA and auto-exposure are planned only (`R-map:72`).
- Depth of field and motion blur: not found; there are no motion vectors (`R-README:193`).

### Culling and LOD

- CPU frustum cull, GPU compute cull into indirect draws, two-phase Hi-Z occlusion culling (`R-README:198-213`; ADR-0057).
- Discrete mesh LODs chosen on the GPU by projected error; made by the app calling `GenerateLods`, not at import (`packages/FenrirAssets/README.md:7-21`).
- `GetFrustum(camera)` and `GetWorldBounds(entity)` for a game's own decisions (`Renderer.hpp:347,376`).

### Particles, billboards, sprites, trails, decals

- **None exist.** Grep for `particle|billboard|sprite|decal|trail|vfx` across headers, docs, C# and shaders finds only plans and unrelated words.
- **Particles are planned as one line**, "Particles.", under "Not yet specified" (`R-map:79`). "2D and sprites" are out of that map's scope (`R-map:97`).
- **Nearest existing facilities**: an `InstanceList` rewritten each frame with `Replace` (per-copy transform and colour; LightingDemo's C# Many lights scene does this); `Additive` and `AlphaBlend` materials; `WorldQuad2D` with a 2D draw list, which has a fixed orientation and no auto-facing (`packages/FenrirRenderer/include/FenrirRenderer/WorldTargets2D.hpp:29-34`). Dynamic meshes are planned only (`R-map:73`).

### Camera

- `Camera { projection, enabled, order, viewport, target, background, drawDebug, look, ambientOcclusion, reflections, debugView }`; looks along local -Z; perspective (60 degrees, infinite far, reversed Z) or orthographic (`Components.hpp:87-107, 809-832`; ADR-0026).
- Every enabled camera draws; `order` and `viewport` compose them; a camera can target an image asset (`Components.hpp:206-219, 427-433`).
- `Renderer::ScreenPointToRay`, `WorldToScreenPoint`, `CameraAt` (`Renderer.hpp:367-382`). No mesh picking (`R-README:233`).
- **No camera controller in the engine.** The free-fly camera is scaffold code; the character follow camera and vehicle chase camera are PhysicsDemo scene code (`examples/PhysicsDemo/src/Character.cpp:105-147`).

### From C#

- Mirrors: `MeshRenderer`, `InstancedMeshRenderer`, `SkinnedMesh`, `Camera` (with all look, AO and reflection fields), the four lights, `WorldQuad2D`; scene resources `Sky`, `SkyLight` (`packages/FenrirRenderer/Managed/Fenrir.Renderer/Generated/`).
- `Renderer.SpawnModel`, `ScreenPointToRay`, `WorldToScreenPoint`, `CameraAt`, `GetWorldBounds`, `GetFrustum`, `Stats`, shadow budgets and culling switches (`Generated/Renderer.cs:20-138`).
- C++-only: `RendererConfig` (MSAA, cascades), `SetVsync`, adding shaders, raw `Overlay`/`WorldTargets2D` submits.

---

## Assets

Package `FenrirAssets` (`packages/FenrirAssets/README.md`; ADR-0021).

- **Formats**: models glTF/GLB (cgltf) and FBX/OBJ (ufbx); images PNG, JPEG (RGBA8 sRGB) and HDR (RGBA32F); `.cubemap` text files naming six faces; fonts `.ttf/.otf/.ttc` (`AssetStore.hpp:222-246`). KTX, DDS and compressed textures: not found; KTX2 is planned only (`R-map:84`).
- **Skeletons and clips** import from glTF/GLB and from FBX (see the contradiction note at the top).
- **API**: `Load<T>(path, owner)` returning handle plus diagnostic, `Add<T>(data, owner)`, `AddBuiltin`, `Replace`, `Unload`, `TryGet`, `IsValid`, `Revision`, `UpdateImage` (`AssetStore.hpp:249-380`). Handles are plain `{index, generation}` values. Sub-assets load by label: `file.glb#Mesh0`, `#Material0`, `#Image0`, `#Skeleton0`, `#Clip0` (`packages/FenrirAssets/include/FenrirAssets/Model.hpp:95-127`).
- **Loading is synchronous and main-thread only** (`AssetStore.hpp:207-209, 223`). `LoadAsync` is planned only (`R-map:84`). LightingDemo's showcase scene stalls about 15 s in Debug on entry (`examples/LightingDemo/README.md`).
- **No offline pipeline or cook step**: source formats are imported at run time each run; shaders are the only build-time step. "An asset database with cooking and hot reload of art" is out of scope (`R-map:96`).
- **No asset hot reload or file watching** in FenrirAssets (grep `watch|hot.?reload|last_write_time`: nothing). `Replace` and material setters bump a revision and the renderer re-uploads.
- **Paths**: one root, `AssetsConfig::root`, default `assets/` beside the executable (`AssetStore.hpp:91-98`). No search path or virtual file system. Demos copy asset folders there after each build (`examples/DemoScaffold/cmake/FenrirDemoApp.cmake:51-63`).
- **Importer limits noted in the repo**: no `KHR_texture_transform` or per-texture `texCoord` (`examples/LightingDemo/README.md`); morph targets are skipped with a warning (`packages/FenrirAssets/src/Importers/Cgltf/CgltfModelImporter.cpp:561-565`).
- **From C#** (`packages/FenrirAssets/Managed/Fenrir.Assets/`): `Assets.Load<T>`, `Unload`, `IsValid`, `AddMesh(positions, normals, uvs, indices)`, `ReplaceMesh`, `GenerateLods`, `AddImage`, `AddRenderTarget`, `AddInstanceList`, `ReplaceInstanceList`; `Materials.Create/Clone/Set*/SetBlendMode/SetDoubleSided`; `Shaders.GetParameter`.
- **Assets shipped in the repo**: `examples/assets/` has `cube.obj`, `sphere.obj`, `BoxTextured`, two skyboxes and icons. Animated models are under `examples/AnimationDemo/assets/` (see Animation).

---

## Physics

Package `FenrirPhysics` over JoltPhysics, hidden at compile level (ADR-0010). Components are plain data that physics diffs each tick (ADR-0011).

### Setup

- `Physics::Install(app, PhysicsConfig{gravity (0,-9.81,0), maxBodies, maxContacts, collisionLayers, useWorkerThreads, interpolate})` (`Phys/Physics.hpp:27-76`). One world per scene, created when the scene first runs active with a body in it; `PhysicsWorld::Of(scene)` (`Phys/PhysicsWorld.hpp:77`).
- Steps once per fixed tick in `PostTick`; interpolated poses are written in `AfterTicks` and true poses restored in `BeforeTicks` (`Phys/Physics.hpp:67-74`).

### Rigid bodies and shapes

- `RigidBody { motionType Static|Kinematic|Dynamic, mass, linearDamping, angularDamping, gravityScale, linearVelocity, angularVelocity, interpolate }` (`Phys/Components.hpp:12-59`). A `Collider` with no `RigidBody` is static.
- **Kinematic bodies (moving platforms)** are moved by writing `Transform` in `Tick`; they push dynamic bodies (`Phys/Components.hpp:32-33`; demo `examples/PhysicsDemo/src/Character.cpp:815-874`).
- Forces: `PhysicsWorld::AddImpulse/AddForce/AddTorque/AddAngularImpulse` (`Phys/PhysicsWorld.hpp:196-216`).
- **Shapes**: `Box`, `Sphere`, `Capsule`, `ConvexHull`, `Mesh` (`Phys/Components.hpp:61-125`). One `Collider` per entity; compounds are colliders on child entities; no offset field (an offset is a child entity); entity scale is baked into the shape (`Phys/Components.hpp:26-28, 261-270`).
- **Level geometry**: `Physics::CreateCollisionMesh(vertices, indices)`; "Loading the data from a model or file is the caller's job" (`Phys/Physics.hpp:79-90`). No mesh-asset-to-collider helper; PhysicsDemo flattens a loaded model by hand (`examples/PhysicsDemo/src/Playground.cpp:54-90`). A collision asset pipeline is out of scope (`P-map:100-107`).
- `PhysicsMaterial { friction, restitution }` (`Phys/Components.hpp:276-292`).
- Not found: CCD, axis or rotation locking, cylinder and heightfield shapes (searched the public headers and `JoltBackend.cpp`).

### Triggers, contacts, layers

- `Trigger { enabled }` on a collider entity: reports overlaps, never pushes, does not see other triggers (`Phys/Components.hpp:311-324`).
- Events: `TriggerEnter`, `TriggerExit { trigger, otherBody, otherCollider }`; `ContactBegin { bodyA, bodyB, colliderA, colliderB, point, normal, relativeSpeed }`, `ContactEnd`; `JointBroken` (`Phys/Events.hpp:65-133`). Contact events are opt-in with a `ContactEvents` component on either body (`Phys/Components.hpp:326-333`).
- **No stay events** (`P-map:39`). Polling: `PhysicsWorld::ForEachContact(entity, fn)` and `Overlap` (`Phys/PhysicsWorld.hpp:90,146`).
- **A character body sets off triggers and contact events**: `PhysTests/CharacterAmongBodies.cpp:95-120` requires one enter and one exit naming the character; `PhysTests/CharacterContacts.cpp:94-195` covers contacts.
- `CollisionFilter { layers, mask }`, 32 layer bits, symmetric rule; optional names through `PhysicsConfig::collisionLayers` (`Phys/Components.hpp:294-309`; `Phys/CollisionLayers.hpp:15-45`).

### The character body

`CharacterBody` (`Phys/Characters.hpp:134-162`; ADR-0014), beside the entity's own `Collider`.

- **Fields**: `velocity` (in and out), `maxSlope` (45 degrees), `stepHeight` (0.4 m), `floorSnap` (0.5 m), `pushForce` (100 N), `mass` (70 kg), `stopOnSlope`, `interpolate`, `enabled`, and read-back `state`.
- **`CharacterState`** (`Phys/Characters.hpp:27-65`): `ground` (`InAir | OnGround | OnSteepGround | Touching`), `groundBody`, `groundCollider`, `groundPoint`, `groundNormal`, `groundVelocity`, `groundAngularVelocity`, `movedVelocity`, `carriedVelocity`, `onWall`, `onCeiling`, `shapeBlocked`.
- **Velocity-driven**: game code writes `velocity` in `Tick`; physics moves the character once in `PostTick` and writes `velocity` back trimmed (landing clears falling speed, a ceiling clears upward speed) (`Phys/Characters.hpp:67-78`). There is no move call.
- **Physics adds no gravity.** Game code adds `PhysicsWorld::Gravity() * dt` and sets `velocity.y` to jump (`Phys/Characters.hpp:77-78`).
- **Slopes, stairs, floor snap** are built in (`PhysTests/CharacterSlopes.cpp`).
- **Moving platforms are automatic**: the ground's velocity is added to the move on walkable ground; leaving the ground keeps it as `carriedVelocity` until landing; platform rotation is reported, not applied; the character trails a descending platform by a tick (`Phys/Characters.hpp:87-92`; `PhysTests/CharacterPlatforms.cpp`).
- **Pushing**: it pushes dynamic bodies up to `pushForce`; dynamic bodies never push it; a kinematic body shoves it; characters block each other (`Phys/Characters.hpp:112-132`).
- **Shape**: box, sphere, capsule or convex hull; crouch is an edit of `Collider.shape`, applied when it fits (`PhysTests/CharacterShapes.cpp`). Up is always world +Y.
- **Teleport**: write the `Transform` position (`Phys/Characters.hpp:98-103`).
- **Left to the game**: jump, double jump, coyote time, jump buffering, air control, acceleration, gravity, facing. The only mention in the package is "Landing and coyote time are game code" (`packages/FenrirPhysics/Managed/Fenrir.Physics/Characters.cs:13`).
- **Demo controllers**: C++ `CharacterScene::Tick`, core about 19 lines, single jump at 5 m/s when `OnGround`, walk 4 and run 8 m/s, acceleration 30 m/s² (`examples/PhysicsDemo/src/Character.cpp:226-271, 1144-1147`). C# `CharacterController`, `[System(Phase.Tick, Order = 10)]` (`examples/PhysicsDemo/scripts/Character.cs:538-597`), plus a `MoveCast` dash and `Depenetrate` push-out (:646-676). The whole C# character scene is 858 lines.

### Queries, joints, vehicles, ragdolls

- **Queries** (`Phys/PhysicsWorld.hpp:98-169`; `Phys/Queries.hpp`): `Raycast`, `RaycastAll`, `ShapeCast`, `ShapeCastAll`, `MoveCast`, `Overlap`, `OverlapPoint`, `Penetrations`, `Depenetrate`, with `QueryFilter { mask, layers, ignoreBody, hitTriggers }`.
- **Joints** (ADR-0013; `Phys/Joints.hpp:166-380`): `Fixed`, `Ball`, `Distance`, `Hinge`, `Slider`, `SwingTwist` on joint entities, with limits, motors, springs and breaking.
- **Vehicles** (ADR-0015; `Phys/Vehicles.hpp`): wheeled, motorcycle, tracked and trailer vehicles on a chassis body with wheel entities.
- **Ragdolls are not built.** No `FenrirRagdoll` package and no `Ragdoll` type in `packages/` or `examples/`. The spec and its eight tickets are `Status: ready-for-agent` (`.scratch/fenrir-physics-ragdolls/spec.md`, `issues/01-08`; ADR-0053, 0054). `CollisionGroup` is defined in `GLOSSARY.md:409` but not built (ticket 01 of that spec). AnimationDemo's "Going limp" scene uses demo code standing in for ragdoll glue (`examples/AnimationDemo/src/GoingLimp.cpp`).
- **Debug lines**: `PhysicsWorld::SetDebugDraw(categories)` for colliders, contacts, velocities, bounds, joints, characters, vehicles (`Phys/DebugLines.hpp:21-53`).

### From C#

`Fenrir.Physics` (`packages/FenrirPhysics/Managed/Fenrir.Physics/`): mirrors of `RigidBody`, `Collider`, `PhysicsMaterial`, `CollisionFilter`, `Trigger`, `ContactEvents`, `Joint`, `CharacterBody` (read-only `State`), the vehicle components; all five events through `EventReader<T>`; static `PhysicsWorld` with every query above, forces, `Gravity`, `ContactsOf`, `SetDebugDraw`, `CreateCollisionMesh`. C++-only: `Physics::Install` and its config, named-layer lookup (C# uses raw bits), another scene's world.

---

## Animation and skeletons

Packages `FenrirSkeleton` and `FenrirAnimation` (ADR-0028, 0031, 0033-0041).

- **Skeleton**: `SkeletonPose { skeleton, timing }` on a skeleton entity; bones are arrays, not entities (`packages/FenrirSkeleton/include/FenrirSkeleton/SkeletonPose.hpp:43-49`; ADR-0028). `Skeletons` facade for bone lookup and pose reads (`packages/FenrirSkeleton/include/FenrirSkeleton/Skeletons.hpp:81-321`). Skinning is on the GPU in compute (ADR-0050).
- **Clips**: `Clip` assets from glTF/GLB animations and FBX anim stacks; TRS per bone only (`packages/FenrirAssets/include/FenrirAssets/ClipData.hpp:37-123`).
- **Clips from another file** bind to a skeleton by exact bone name; no retargeting across proportions or rest poses (`packages/FenrirAnimation/README.md:43`; `docs/adr/0031-*.md:117`).
- **Playback**: the game writes an `Animator { clip, speed, loop Loop|Once, playing, fadeSeconds, fadeCurve }` on the skeleton entity; the engine owns the clock and writes the pose (`packages/FenrirAnimation/include/FenrirAnimation/Animator.hpp:10-58`; ADR-0034). `Animation::Seek`, `Animation::Playback` (`Animation.hpp:120-144`). Changing `clip` with `fadeSeconds > 0` crossfades. There are no `Play()`/`CrossFade()` calls.
- **Blending**: `AnimationLayer` entities with `order`, `weight`, `Override | Additive` mode and a `BoneMask` (`packages/FenrirAnimation/include/FenrirAnimation/AnimationLayer.hpp:12-68`; ADR-0036).
- **Events**: `ClipEvent { time, name }` added in code (`AssetStore::AddClipEvent`, `AssetStore.hpp:504-519`), delivered as `AnimationEvent`; `ClipFinished` when a `Once` clip ends (`packages/FenrirAnimation/include/FenrirAnimation/AnimationEvents.hpp:24-72`). Events from file markers are not imported.
- **Attachments**: `BoneAttachment { skeleton, bone, offset }` places any entity at a bone (`packages/FenrirSkeleton/include/FenrirSkeleton/BoneAttachment.hpp:21-62`; ADR-0037).
- **Pose modifiers**: a generic modifier stage (`Skeletons::AddModifier`, `PoseEdit::Write`) and the built-in `PoseOverride` with per-bone weights (`packages/FenrirSkeleton/include/FenrirSkeleton/PoseStages.hpp:23-243`).
- **Not present, by the repo's own statements**:
  - State machines, blend trees, blend spaces, sync groups (`docs/adr/0036-*.md:21,195`). Demos sequence clips from key presses by writing `Animator.clip`.
  - Root motion extraction (`packages/FenrirAnimation/README.md:24`; `docs/adr/0034-*.md:154`). Locomotion clips with root translation travel and snap back.
  - Node animation of plain entity transforms: such channels are kept but marked not playable (`ClipData.hpp:69-73`; `docs/adr/0034-*.md:154`). A spinning coin or a platform path authored in a modelling tool does not play.
  - Morph targets (`CgltfModelImporter.cpp:561-565`).
  - IK and look-at (`docs/adr/0033-*.md:85-87`).
  - Ping-pong looping and a global animation time scale (`docs/adr/0034-*.md:154`).
- **Animated assets in the repo** (`examples/AnimationDemo/assets/ATTRIBUTION.md`): a procedural mannequin (Idle, Walk, Run, Wave, Flutter), a critter, Quaternius `rig-human.glb` with idle, walk, jog, sprint and one more clip (CC0), Fox (Survey, Walk, Run), CesiumMan (walk), Kenney `characterMedium.fbx` with idle and run (CC0). No jump, fall, land or celebrate clip ships.
- **From C#** (ADR-0039): mirrors `SkeletonPose`, `BoneAttachment`, `Animator`, `AnimationLayer`; `Skeletons.*` reads and overrides; `Animation.Seek/Playback`; `Clips.AddEvent/...`; `BoneMasks.Create/Include/Clear`; events through `EventReader<T>`; C# stage systems `[StageSystem(PoseStages.Modify)]` (`packages/FenrirSkeleton/Managed/Fenrir.Skeleton/`, `packages/FenrirAnimation/Managed/Fenrir.Animation/`). Clips load with `Assets.Load<Clip>("file.glb#Clip0")`.

---

## UI

Packages `FenrirUI` (Yoga flexbox), `FenrirText` (HarfBuzz, FreeType), `FenrirPainter`, `FenrirDraw2D` (ADR-0060 to 0072).

- **Model**: retained tree of elements addressed by versioned `ElementId`, not entities (ADR-0063, 0066). `UI::Install(app)`; `UIRoots::CreateRoot(UIRootConfig)`; any number of roots, ordered by `layer`, with a separate `inputPriority` (`packages/FenrirUI/include/FenrirUI/UI.hpp:109-236`; `UIRoot.hpp:76-139`).
- **Where a root lives**: the window overlay, an image asset, or a world quad on an entity (`UIRootConfig::target`, `UIRoot.hpp:97-102`).
- **Authoring**: a C++ code tree (`Create`, `SetText`, `AddClass`, `SetStyleProperty`); HTML-flavoured markup (`LoadDocument`, `LoadMarkup`); a CSS subset (ADR-0064; `UI-README:47-84`).
  - Tags: `panel`, `label`, `button`, `checkbox`, `slider`, `progress`, `image`, `scroll`, `textentry`, plus custom elements registered from C++ (`packages/FenrirUI/include/FenrirUI/Controls.hpp:45-404`).
  - CSS: flexbox layout properties, absolute positioning, backgrounds with gradients and `url()` images, nine-slice `border-image`, radii, `box-shadow`, `filter: blur()/drop-shadow()`, `backdrop-filter: blur()`, `opacity`, `transform`, text properties with `text-shadow`, custom properties, `@keyframes`, `@font-face`, `@import`, transitions (`packages/FenrirUI/include/FenrirUI/Style.hpp:636-727`).
  - Selectors: type, class, id, descendant, child, `:hover :active :focus :focus-within :disabled :checked :intro :outro :first-child :last-child`, nesting.
  - Not supported: `!important`, `*`, sibling and attribute selectors, `calc()`, `@media`; `em/rem/vw/vh` were not found.
- **Scaling**: `ScaleRule::Height1080` by default, so 1080 UI units span the window height (`UIRoot.hpp:38-47`).
- **Hot reload** of stylesheets and markup documents by polling file times, per root, off by default (`UI-README:88-90`).
- **Focus and gamepad**: Tab order, spatial D-pad and left-stick navigation with A to activate and B sending `Cancel`, and a virtual cursor (`UI-README:35,42,43`). Keyboard arrow keys do not move focus.
- **Events to the game**: `UIEvent` read with `EventReader<UIEvent>` (Click, Change, Submit, Cancel, pointer, key, focus kinds; `packages/FenrirUI/include/FenrirUI/UIEvent.hpp:21-64`), or C++ listeners. **Data to the UI** is pushed by systems; no binding language in C++ or markup (ADR-0065).
- **Pointer opt-in**: elements ignore the pointer unless `pointer-events: all`, so a HUD is transparent to input (`UI-README:31`).
- **Text**: one shipped font, JetBrains Mono (`packages/FenrirText/Resources/Fonts/JetBrainsMono-Regular.ttf`), the fallback of every family; games add faces with `@font-face` or `AddFontFace`; bitmap atlases by default, SDF per face for world or scaled text (`packages/FenrirText/README.md:18-48`). One style per element's text; no inline spans in markup.
- **Animation**: CSS transitions and keyframes; `:intro` on creation and `:outro` before removal (ADR-0066).
- **World-space UI**: an entity with `Transform` and `WorldQuad2D`, a root targeting it; the game supplies a pointer ray with `SetPointerRay`; no billboard option (UIDemo turns its name plate in game code, `examples/UIDemo/src/Room.cpp:397-402`).
- **Razor panels** (C#; ADR-0067 to 0070): a `.razor` file is a `UIComponent` compiled by the stock SDK generator against an engine stand-in; mounted with `root.Mount<T>(element)`; `@onclick` and other handlers, `@bind` on slider, progress, checkbox and textentry; scoped `.razor.css`. A `.razor` change needs a script rebuild, after which panels remount and lose local state (`UI-README:126-235`). Smallest self-contained example: `examples/UIDemo/scripts/Speedometer.razor` (50 lines) with `SpeedometerSetup.cs`.
- **C# without Razor**: `UIRoot.Create/CreateWorldRoot/Find`, `CreateElement`, `FindById`, `AddClass`, `SetStyle`, `SetText`, `SetValue`, `LoadDocument`, `LoadStylesheet`, `Focus`, `SetPointerRay` (`packages/FenrirUI/Managed/Fenrir.UI/Generated/UIRoot.cs:21-337`); `EventReader<UIEvent>`.
- **Immediate painting**: `Painter` draws rects, rounded rects, circles, arcs, paths, images and text into a `DrawList2D` submitted to the window overlay; from C#, `Painter.Overlay(window, layer)` (`packages/FenrirPainter/include/FenrirPainter/Painter.hpp:131-264`).
- **What a HUD and a menu are built from, as UIDemo shows** (`examples/UIDemo/README.md`): a HUD root at layer 0 (`hud.ui` 40 lines, `hud.css` 246, `Hud.cpp` 181) with progress bars, labels, a custom-painted compass, a render-target minimap and intro/outro hit markers; a pause menu root at layer 10 with higher input priority (`menu.ui`, `settings.ui`, `menu.css`, `Menu.cpp` 399 lines) with buttons, sliders, checkboxes, a text entry and a scrolling list.
- **Not provided**: pausing gameplay behind a menu (UIDemo does it in game code with a `Menu` resource, `examples/UIDemo/README.md:37-39`); a menu stack; dropdown, tabs, radio group, tooltip, popup layer, modal dialog (`docs/adr/0071-*.md:27,45`); sprite-atlas sub-rectangles and a CSS tint for images; SVG; localisation tables; a UI inspector.
- **The demo UI kit** (readouts, toggles, ranges, choices in a dev panel) is scaffold code under `examples/DemoScaffold/` (ADR-0071, 0072).

---

## Scripting

Packages `FenrirScripting` and `Iris` (ADR-0001 to 0009, 0029, 0042 to 0046).

- **ECS-first, not behaviour-per-entity** (ADR-0007). A script is C# components and systems:
  - `struct Spin : IComponent` and `struct Hit : IEvent` are defined in C# with no C++.
  - `[System(Phase.Update, Order = n)] sealed class Foo : ISystem { void OnUpdate(in SystemContext ctx) }`, found by a source generator (`packages/FenrirScripting/Managed/Fenrir.Scripting/Systems.cs`).
  - Smallest example: `examples/ScriptingDemo/scripts/Spin.cs` (86 lines).
- **Phases from C#**: `PreUpdate`, `Tick`, `Update`, `PostUpdate` only. Order by `Order`, then type name. Main thread only.
- **Queries**: `ctx.Query<A, B>()` over 1 to 8 components, native and C# mixed, with in-place `Ref<T>` writes; no without/any/changed filters found.
- **Entities**: `Entity.Create()`, `Add<T>`, `Remove<T>`, `Destroy`, `Has<T>`, `Get<T>`, `WorldTransform`, `SetWorldTransform` (`Entity.cs:69-164`). Structural changes are deferred until the system returns (ADR-0029).
- **Resources and events**: `Resources.Get<T>()` for mirrored native resources; `EventReader<T>` fields and `ctx.Events.Send`. Resources defined in C# do not exist (`docs/adr/0029-*.md:42`).
- **Core facades**: `Log.Info/Warn/Error`; `Time.DeltaTime`, `Time.TickRate` (read-only).
- **Hot reload**: the host polls the built script DLL and reloads it when it changes; rebuilding is external (`dotnet build` or the demo's `<App>DeployScripts` target). C# component data survives when the type's layout fingerprint is unchanged; system instances and their fields do not (`SR`, "Hot reload"; ADR-0005).
- **A throwing system** is logged once and disabled until the next reload (`Systems.cs`).
- **Host side**: `Scripting::Install(app, ScriptingConfig{ .scriptAssembly, .extensions, .hotReload, ... })` (`packages/FenrirScripting/include/FenrirScripting/Scripting.hpp:22-77`). Each package must be installed in C++ and its `ScriptingExtension()` listed, or it is not scriptable (Scripting.hpp:42-49; list at `examples/DemoScaffold/src/DemoScripting.cpp:44-66`). No engine-provided generic host executable exists outside the scaffold.
- **How much can be C#**: PhysicsDemo's "(C#)" scenes are built and run wholly from scripts, with C++ supplying only a scene descriptor (`examples/PhysicsDemo/README.md:54-62`; `scripts/Character.cs` 858 lines against `src/Character.cpp` 1319).

**Reachable from C#** (pointers in the sections above): input and cursor capture; gamepad; loading models, clips, images; spawning models; creating materials and setting colours; cameras, lights, sky and all post-processing fields; physics components, queries, forces, trigger and contact events, the character body; animators, layers, clip events, bone attachments, pose overrides; UI roots, elements, events and Razor panels; painter; debug draw; logging; window title, close request and clipboard.

**Not reachable from C#**:

- Creating, switching or destroying scenes (`App::CreateScene/ChangeActiveScene`, App.hpp:620-632). C# always acts on the active scene; the demos switch scenes in C++.
- `App::Stop`. Quitting is indirect: `Window.RequestClose(Window.Main)` stops the app when `WindowConfig::stopOnMainWindowClose` is true, the default.
- Setting the tick rate; elapsed time, frame and tick counters.
- General entity parenting: `Relationship` is read-only in C# and `Entity` has no set-parent; parents can be given only through `Renderer.SpawnModel(model, parent)`, `Skeletons.SpawnModel` and `Skeletons.Detach`.
- Adding custom shaders, `RendererConfig`, vsync.
- Input routers, custom UI `Element` subclasses, UI listeners, scroll offsets, `gamepadNavigation` on a root (`packages/FenrirUI/Managed/Fenrir.UI/Generated/RootSettings.cs`).
- Named collision-layer lookup.
- Package installs and scheduler configuration.

**Nothing engine-provided for**: save data or file IO (scripts are ordinary `net10.0` assemblies and no sandbox was found, but no doc states a policy and no script uses `System.IO`), random numbers, timers, tweens, coroutines or async (`Ref<T>` cannot be used in async methods, `SR` near line 110; async is out of scope for Iris, `packages/Iris/IMPLEMENTATION_PLAN.md:49`).

**Debugging C#**: debugger attach is listed as out of scope in `packages/Iris/IMPLEMENTATION_PLAN.md:52`; no documentation on attaching was found. The reload copy includes the `.pdb`.

**An unresolved detail**: `examples/ScriptingDemo/scripts/Spin.cs:25` says `bool` is not allowed in a C# component and uses `int`; `examples/PhysicsDemo/scripts/Character.cs` (near line 77) uses `bool` fields in one. Which is the rule was not established.

---

## Levels and persistence

- **No scene, level or prefab file format exists.** Grep for `serializ|serialis|prefab|savegame|save data` across headers, C#, READMEs, ADRs and `GLOSSARY.md` finds no scene or entity serialisation; the hits are Iris internals, shader containers and "avoid" notes. `GLOSSARY.md:807` lists "prefab" as a word to avoid for a spawned model.
- **No entity or component serialisation, and no reflection-driven save.** The API descriptions (`packages/<Package>/api/*.api.json`, ADR-0042) describe types for binding generation; nothing reads them at run time to save or load entities.
- **No save-data API.**
- **No editor.** "The engine has no editor yet" (`docs/adr/0064-*.md:33`); the editor is "a prototype of its own" and out of the renderer map's scope (`R-map:95`).
- **What a level is today**: code. Every demo scene is a C++ `spawn` function or a C# builder system that creates entities (`examples/DemoScaffold/README.md:48-50, 58-115`).
- **The nearest thing to an authored level**: a glTF/GLB/FBX model loaded as a `Model` and spawned with `SpawnModel`, which gives an entity per node with its name, transform, meshes, materials and punctual lights (`SpawnModel.hpp:15-43`). Collision for it is built by hand from the mesh data (see Physics). Whether glTF node `extras` (custom properties) are imported was not checked.
- **Markup documents and stylesheets** are the only authored, hot-reloaded data files, and they describe UI only.

---

## Debugging

- **FenrirDebugDraw** (always built; ADR-0025): per-scene `DebugDraw` resource with `Line`, `Ray`, `Arrow`, `Polyline`, `Point`, `Box`, `Sphere`, `Capsule`, `Cylinder`, `Cone`, `Circle`, `Arc`, `Plane`, `Grid`, `Axes`, `Frustum`, solid box, sphere and capsule, `Triangles`, `ScreenText`, keyed `Message`, `WorldText`, with durations, depth modes and named groups that can be shown or hidden (`packages/FenrirDebugDraw/include/FenrirDebugDraw/DebugDraw.hpp:170-318`). The same from C# (`packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/DebugDraw.cs`).
- **Physics debug lines** by category (see Physics). **Camera debug views**: AO, shadow cascades and atlases, light clusters, reflections (`Components.hpp:754-797`). More debug views are planned only (`R-map:71`).
- **`RendererStats`** through `Renderer::Stats()` (`Renderer.hpp:162, 353`); `UIRootStats` (`UIRoot.hpp:161-175`).
- **Logging**: `ILogger`/`ConsoleLogger` in C++, `Log` in C#.
- **In-game debug panels**: none in the engine. The demo UI kit (readouts, toggles, sliders, frame stats, key help) is scaffold code. No console, entity inspector or profiler was found (grep `profiler` in packages, docs and `.scratch`: nothing).
- **Deterministic driving**: `App::RunFrame(dt)` and the null renderer backend let tests and smoke runs drive the app without a clock or a GPU (App.hpp:552; README.md:87).

---

## What each demo shows

| Demo | Shows | Game-shaped code it writes itself |
| --- | --- | --- |
| RendererDemo (`examples/RendererDemo/README.md:14-23`) | Meshes and materials with a custom shader, cameras and render targets, sky, debug draw, overlay, painter; two C# twins | Scene setup only, 242 to 355 lines a scene |
| LightingDemo (`examples/LightingDemo/README.md`) | IBL material grid, showcase models, 1024 clustered lights, rect lights with SSR, 100k instanced rocks with culling and LODs, a forest with custom grass shaders, a neon city with the full lens stack, Sponza | Procedural terrain, city and car lists; up to 1163 lines a scene |
| PhysicsDemo (`examples/PhysicsDemo/README.md:50-61`) | Bodies, triggers, joints, the character on slopes, stairs, lifts and push boxes, vehicles; C# twins of four scenes | The whole character controller, follow camera, platform movers, crouch; the vehicle input mapping |
| AnimationDemo (`examples/AnimationDemo/README.md:21-31`) | Playback, skeleton views, blending and masked layers, bone attachments, pose overrides, a 1024-character crowd; two C# scenes | Clip sequencing from keys; glue that moves a kinematic shield with a bone |
| ScriptingDemo (`examples/ScriptingDemo/README.md`) | C# components, systems, input, spawn and destroy, events, a Razor panel, hot reload | Small; C++ spawns boxes, C# drives them |
| UIDemo (`examples/UIDemo/README.md`) | HUD, pause menu with settings, a wall screen, two world-space roots; Razor speedometer and settings | Menu open/close state, pausing, pointer rays for world UI, the compass element |

No demo is a game: none has a win or lose state, a level sequence, a score or a timer.

---

## Missing for a platformer

Each gap, with whether anything in the repo plans it. "No plan found" means no map, spec, ticket or ADR names it as future work.

**Whole systems absent**

1. **Audio.** No package. SDL's audio subsystem is switched off (`docs/adr/0020-*.md:18`). Out of the renderer map's scope (`R-map:97`); the Flightless Frenzy map records it as out of scope and an engine gap (`.scratch/flightless-frenzy/map.md:31`). No plan found.
2. **Scene, level and prefab files; entity serialisation; save data.** None. No plan found.
3. **An editor or any level-authoring path.** None; the editor is "a prototype of its own" outside the renderer map (`R-map:95`). An editor "Build scripts" action is listed as carried-over future work (`R-map:67`). No plan for level authoring.
4. **Particles.** None. One line under "Not yet specified" (`R-map:79`); no spec.
5. **Billboards, sprites, trails, decals.** None. "2D and sprites" are out of the renderer map's scope (`R-map:97`); decals and trails have no plan found.
6. **Input actions and bindings, rebinding, rumble, stick dead zones.** None; "out of scope" in `packages/FenrirInput/README.md` ("Not here yet"). No plan found.
7. **Navigation or AI for enemies** (navmesh, pathfinding, behaviour trees). None (grep `navmesh|pathfind|behaviour tree`: nothing). No plan found.
8. **Time scale, pause, timers, tweens, coroutines, random numbers.** None engine-provided. Only a global animation time scale is named as not yet specified (`docs/adr/0034-*.md:154`). No plan found for the rest.

**Rendering gaps**

9. **Custom post-process or render passes, outlines, stencil, scene depth or normals in material shaders.** Not available to game code. Only "a user pass in `Cull`" is a not-yet (`docs/adr/0057-*.md:40`). No plan found for outlines or user passes.
10. **A toon or cel material.** None shipped; custom material shaders are supported, so it is game-side shader work. No plan found.
11. **Per-entity material overrides or tint on `MeshRenderer`.** Planned only (`R-map:87`).
12. **Fog and volumetric light.** Planned only, never triaged (`R-map:72`).
13. **Anti-aliasing beyond install-time MSAA; auto-exposure.** Planned only (`R-map:72`).
14. **Built-in primitive meshes** (cube, sphere, plane, capsule). None in the engine. No plan found.
15. **Order-independent or per-triangle-sorted transparency.** None. No plan found.
16. **Shader hot reload; loading a shader from a file path; adding a shader from C#.** Hot reload planned only (`R-map:83`); the others have no plan found.
17. **Dynamic meshes.** Planned only (`R-map:73`).
18. **A second graphics backend; macOS.** Out of scope (`R-map:91-99`).

**Asset gaps**

19. **Asynchronous loading and load states (loading screens).** Planned only (`R-map:84`).
20. **Asset hot reload, cooking, an asset database.** Out of scope (`R-map:96`).
21. **Compressed textures (KTX2).** Planned only (`R-map:84`).
22. **Collision from a model asset.** By hand through `CreateCollisionMesh`; a collision asset pipeline is out of scope (`P-map:100-107`).
23. **glTF `KHR_texture_transform` and per-texture UV sets; import options.** Not imported; import options planned only (`R-map:84`).

**Physics and gameplay gaps**

24. **Character gameplay: gravity, jump, double jump, coyote time, jump buffering, air control, acceleration, facing.** Game code by design (ADR-0014). Not a planned engine feature.
25. **Camera controllers** (first-person look, follow, orbit). Demo code only. No plan found.
26. **Trigger stay events.** None by decision (`P-map:39`); polling through `Overlap` or `ForEachContact`.
27. **CCD, axis locking, cylinder and heightfield shapes, collider offset field.** Not found. No plan found.
28. **Ragdolls and collision groups.** Specified, not built: `.scratch/fenrir-physics-ragdolls/spec.md` and tickets 01-08, all `ready-for-agent`.
29. **Rotation carried from a turning platform.** Reported in `groundAngularVelocity`, not applied (`Phys/Characters.hpp:87-92`). No plan found.

**Animation gaps**

30. **State machines, blend trees, blend spaces, sync groups.** Not in the spec (`docs/adr/0036-*.md:195`); listed under "Richer animation" as not yet specified (`R-map:74`).
31. **Root motion.** Not yet specified (`R-map:74`; `docs/adr/0034-*.md:154`).
32. **Node animation of plain entities** (spinning pickups, authored platform paths). Not yet specified (`R-map:74`; `docs/adr/0034-*.md:154`).
33. **Morph targets, IK, look-at, retargeting, clip events from file markers, ping-pong looping.** Not yet specified (`R-map:74`; `docs/adr/0031-*.md:160`; `docs/adr/0033-*.md:85-87`).
34. **Jump, fall, land and celebration clips.** None among the repo's assets. Not an engine plan.

**UI gaps**

35. **Dropdown and colour controls.** Named as follow-up engine work in `docs/adr/0071-*.md:27`, pointing at a `.scratch/fenrir-ui-followups/` directory that is not in this commit.
36. **Popup layer, tooltips, modal dialogs, tabs, radio groups, a menu stack.** None (`docs/adr/0071-*.md:45`; `examples/UIDemo/README.md:38-39`). No plan found.
37. **Pausing the game behind a menu.** Game code. No plan found.
38. **Arrow-key focus navigation.** Not found (Tab and gamepad only). No plan found.
39. **Inline styled text spans, text outline, localisation tables, colour emoji, system font fallback.** Not found; emoji, variable fonts and system fallback are "not yet" (`packages/FenrirText/README.md:50`).
40. **Image sub-rectangles from an atlas and a CSS image tint; SVG.** Not found. No plan found.
41. **A proportional UI font.** Only JetBrains Mono ships. Not an engine plan.
42. **Bindings in markup; async Razor handlers; Razor routing and injection.** Not in this version (`docs/adr/0069-*.md:29`; `docs/adr/0068-*.md:19`).
43. **A UI inspector.** Not found. No plan found.
44. **Billboarded world-space UI.** Not found; game code turns the quad. No plan found.

**Scripting gaps**

45. **Scene create, switch and destroy from C#; `App.Stop`; tick-rate and elapsed-time access.** Not bound. No plan found.
46. **Entity parenting from C#.** `Relationship` is read-only. No plan found.
47. **Resources defined in C#.** "Left for later" (`docs/adr/0029-*.md:42`).
48. **Query filters; querying read-only components.** Read-only components cannot be queried "yet" (`SR` near line 441); filters have no plan found.
49. **C# debugger attach.** Out of scope in `packages/Iris/IMPLEMENTATION_PLAN.md:52`.
50. **Per-entity behaviour objects.** Only "a possible later layer" (`docs/adr/0007-*.md:11`).

**Running and shipping gaps**

51. **Consuming the engine from another project.** No install, export or package for the engine or the `Fenrir.*` contracts; no documentation of it; `fenrir_add_shaders` reads a variable that is not visible from a parent directory (untested). No plan found.
52. **An engine-level way to build a game's C# script project, and a generic script host.** Only the demo scaffold's `fenrir_add_demo_scripts`. No plan found.
53. **A game-facing application layer.** Package installs, scene list, asset copying and smoke runs are in the demo scaffold, which is example code; lifting the demo UI kit into a package is "a later decision" (`docs/adr/0071-*.md:11`).
54. **Scene transitions and loading screens.** None. No plan found (async loading is item 19).
55. **Fullscreen, borderless, resolution and display settings.** Not found in FenrirWindow's public API. No plan found. Several windows are planned only (`R-map:85`).
56. **Distribution: bundling the .NET runtime, a packaged build.** The runtime must be installed or pointed at with `dotnetRoot`. No plan found.
57. **In-game console, entity inspector, profiler.** Not found. No plan found.
58. **Linux in CI; macOS.** The Linux job is commented out (README.md:87); macOS is out of scope (`R-map:91-99`).

---

## Not verified

- Nothing was built or run.
- ADR bodies were mostly read by grep and through the headers and READMEs that cite them, not end to end.
- `Phys/Vehicles.hpp` and `Phys/Joints.hpp` were read for their struct lists and key fields only; `JoltBackend.cpp` by grep.
- World-quad depth and occlusion behaviour for world-space UI.
- Whether glTF node `extras` survive import.
- Whether `fenrir_add_shaders` and the scripting CMake functions work from a project that adds the engine as a subdirectory: read from the CMake sources, not tried.
- Whether a managed debugger can attach to a hosted script.
