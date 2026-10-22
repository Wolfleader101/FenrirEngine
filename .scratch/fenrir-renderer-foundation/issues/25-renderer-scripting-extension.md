# 25: The Fenrir.Renderer scripting extension and C# demo scenes

**What to build:** C# systems build and inspect rendered scenes exactly as C++ does: `MeshRenderer`, `Camera` and `DirectionalLight` components, `Sky` and `AmbientLight` resources, picking, bounds, stats and `SpawnModel` returning a usable root; the demo scaffold runs C# scenes, and RendererDemo gains "Meshes and materials (C#)" (spec: "Scripting: the extensions" (components, resources, `Fenrir.Renderer`, no-op rules), "Demo apps" (scaffold scripting, `Fenrir.Demo`, RendererDemo scene 6), "Testing Decisions" seam 7; ADR-0029, ticket 22).

- **Layout:** contract in `packages/FenrirRenderer/Managed/Fenrir.Renderer/`, native functions in a static `RendererScripting` library; builds with `FENRIR_ENABLE_SCRIPTING` and `FENRIR_ENABLE_RENDERER`; `Renderer::ScriptingExtension()`.
- **Components,** writable straight mirrors (C++ field order in PascalCase, C++ defaults on `new`, `new` equal to `{}`): `MeshRenderer`, `Camera`, `DirectionalLight`. `Camera`'s unions through the `[TaggedUnion]` generator: `CameraProjection { Perspective, Orthographic }`, `RenderTarget { Window, Image }`, `CameraBackground { Scene, Colour }`, with `WindowId` from `Fenrir.Window`.
- **Resources:** `Sky` (its union through the generator) and `AmbientLight` as writable scene-resource mirrors.
- **Functions:** `ScreenPointToRay(camera, Vector2, out Ray)`, `WorldToScreenPoint(camera, Vector3, out Vector2)`, `CameraAt(WindowId, Vector2) -> Entity`; `GetWorldBounds(entity, out Bounds)`, `Stats(out RendererStats)`; `SpawnModel(model, parent = Entity.None) -> Entity` through the change buffer, its root usable at once.
- **No-op rules:** no renderer, no camera or a stale entity → `false`, zero or `Entity.None`; one debug warning per call site; nothing throws.
- **The demo scaffold's scripting:** `DemoScaffold::Install` installs Scripting when built and adds every package's extension; `fenrir_add_demo_app`'s `SCRIPTS` builds and copies the app's C# script project; the shared `Fenrir.Demo` assembly holds the `CSharpScene` marker and the scene-click event, referenced by each app's scripts project. A scene marked as C# runs its C# systems only while active.
- **RendererDemo scene 6a, "Meshes and materials (C#)":** a twin of scene 1 built entirely from C#. Hand checks in the README.

**Blocked by:** 13 (Materials in full: blend modes, textures, user shaders and SpawnModel); 15 (The sky: a colour or a cubemap per scene, overridable per camera); 23 (The Fenrir.Input and Fenrir.Window scripting extensions); 24 (The Fenrir.Assets scripting extension).

**Touches:** `packages/FenrirRenderer/Managed/`, `packages/FenrirRenderer/Scripting/`, `examples/DemoScaffold/`, `examples/RendererDemo/`, `CMakeLists.txt`, `FenrirScripting.sln`

**Status:** ready-for-agent

- [ ] Extension tests: each mirror's layout matches C++ and `new` equals `{}`; a C# system loads a mesh, makes a material, sets a colour and spawns a `MeshRenderer` and `Camera` that `RendererProbe` sees drawn; `SpawnModel` from C# returns a root usable in the same system.
- [ ] `ScreenPointToRay` from C# agrees with C++; `Sky` written from C# reaches the draw list's background; `AmbientLight` written from C# reaches the frame block.
- [ ] With no renderer installed every call no-ops with the documented return.
- [ ] `FENRIR_DEMO_SMOKE` runs RendererDemo's C# scene on null and GL with no errors, with scripting on.
- [ ] Scene 6a by hand: it matches scene 1 (checked by hand).
