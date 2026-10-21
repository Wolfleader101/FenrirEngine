# What does the renderer's scripting extension give C#?

Type: grilling
Status: resolved
Blocked by: 14, 16, 17, 18

## Question

What does C# get from FenrirRenderer (and FenrirWindow and input, if they are scriptable), through scripting extensions as ADR-0009 describes?

- Which components are writable from C#, and how assets (meshes, materials, textures) are named from C#.
- Native functions for screen-to-world rays and window size (debug lines and text are FenrirDebugDraw's own extension, ADR-0025).
- Input from C# (if input's own ticket does not settle it).
- No-op rules when no renderer is installed, matching the physics extension's.
- Contract and library names, built only when scripting is on.

## Answer

ADR: `docs/adr/0029-scripting-extensions-carry-native-resources-and-deferred-structural-changes.md` (amends ADR-0009). Glossary: **Native resource** added, **Scripting extension** extended. All of it goes into the foundation spec.

- **One extension per package**, as physics: `Fenrir.Assets`, `Fenrir.Renderer`, `Fenrir.Window`, `Fenrir.Input` (FenrirDebugDraw's own is ADR-0025's). Each has a contract in `packages/<Pkg>/Managed/Fenrir.<Pkg>/` and its C++ native functions in a static library `<Pkg>Scripting` (`FenrirAssetsScripting`…), so no package links FenrirScripting. Each builds only when `FENRIR_ENABLE_SCRIPTING` and its package's option are on (input's is always on, ticket 21). The app adds `Assets::ScriptingExtension()`, `Renderer::ScriptingExtension()`, `Window::ScriptingExtension()` and `Input::ScriptingExtension()`. A headless server lists the ones it uses.
- **FenrirScripting prerequisite (ADR-0029)**, a ticket of the foundation spec:
  - **Native resources.** A plain-data resource crosses as a mirror: `MakeSceneResource<T>` / `MakeAppResource<T>` (and read-only variants) in the extension, `[NativeResource("…")]` on the C# struct, layout-checked at load. `Resources.Get<T>()` returns a `Ref<T>` or `ReadOnlyRef<T>`, and `TryGet` covers a resource that may be absent. Insert and remove are deferred structural changes.
  - **Facades for resources with behaviour.** `Keyboard`, `Mouse`, `Gamepads` (and `DebugDraw`) are static classes of native functions, so C++ answers and edges follow the phase.
  - **The change buffer on the call context**, so extensions may record structural changes (ADR-0009's later addition).
  - **`Fenrir.Math`**, a pure managed shared assembly in `packages/FenrirMath/Managed/Fenrir.Math`, namespace `Fenrir.Math`: `Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Matrix4x4`, `Colour` (linear floats, `FromSrgb`), `Ray`, `Bounds`. `Vector3` and `Quaternion` move from `Fenrir.Scripting`, and `Transform` stays there. It is shared first by the built-in extension.
- **Components, all writable straight mirrors** (C++ field order in PascalCase, C++ defaults on `new`, `new` equals `{}` tested): `MeshRenderer`, `Camera`, `DirectionalLight`. `Camera`'s three unions use the `[TaggedUnion]` generator: `CameraProjection { Perspective, Orthographic }`, `RenderTarget { Window, Image }` and `CameraBackground { Scene, Colour }`. `WindowId` is a plain struct in `Fenrir.Window`.
- **Resources:** `Sky` and `AmbientLight` are writable scene-resource mirrors, and `InputClaims` is a read-only app-resource mirror.
- **Assets (`Fenrir.Assets`):**
  - `AssetHandle<T>` is a blittable `{Index, Generation}` struct with a phantom `T` (`Mesh`, `Material`, `Image`, `Model`, `Skeleton`, `Clip`, `Shader` marker types). It is stored in components as in C++.
  - `Assets.Load<T>(path, owner = AssetOwner.Scene)` loads by path, by `file#Label` or by `builtin:` name. The scene owner is the active scene. A failure returns an invalid handle and logs the diagnostic, and nothing throws.
  - `Unload`, `IsValid`, and procedural `AddMesh(positions, normals, uvs, indices, owner)` / `ReplaceMesh` take spans and compute bounds. `AddRenderTarget(width, height, owner)` / `ResizeRenderTarget` make camera targets. `AddImage(width, height, rgba8, owner)` / `ReplaceImage` make pixel images. Each replace bumps the revision.
- **Materials:** `Materials.Create(shader, owner)` and `Clone(material, owner)`. Edits by name: `SetFloat`, `SetVector2/3/4`, `SetColour`, `SetTexture` (image and sampler), `SetBlendMode` and `SetDoubleSided`, each with a `TryGet…`. Every edit bumps the revision, and unknown names or wrong types are logged once and ignored, as in C++ (ADR-0024). Variants stay the renderer's.
- **`Renderer` (`Fenrir.Renderer`):** `ScreenPointToRay(camera, Vector2, out Ray)`, `WorldToScreenPoint(camera, Vector3, out Vector2)`, `CameraAt(WindowId, Vector2) -> Entity`, `GetWorldBounds(entity, out Bounds)`, `Stats(out RendererStats)` and `SpawnModel(model, parent = Entity.None) -> Entity`. `SpawnModel` records through the change buffer, and the returned root is usable at once. Points are in the target's coordinates, as C++ (ADR-0026).
- **`Window` (`Fenrir.Window`):**
  - `Window.Main`; `Size` (logical), `PixelSize`, `ContentScale`, `IsMinimised` and `IsFocused`, each by `WindowId`.
  - `CursorMode` (get and set) and `SetCursorShape`; `Clipboard` (get and set); `SetTitle`; `RequestClose`.
  - The window events (resized, focus, close requested) as native events.
  - Strings cross only as parameters, so getters that return text fill a caller's `Span<char>` and return the length, as physics's span functions do.
- **`Input` (`Fenrir.Input`):**
  - Mirrored enums: `Key`, `Scancode`, `KeyModifiers`, `MouseButton`, `GamepadButton` and `GamepadAxis`.
  - Facades with C++'s phase-following edges:
    - `Keyboard`: `IsDown`, `JustPressed` and `JustReleased`, by key or scancode, and `Modifiers`.
    - `Mouse`: buttons, `Position`, `Motion` and `Scroll`.
    - `Gamepads`: `IsConnected`, buttons and `Axis`, per pad.
  - Every input event as a native event read through `EventReader<T>`.
  - Input routers stay C++ (ADR-0027). Cursor mode is the window's.
- **No-op rules, physics's:**
  - `X::ScriptingExtension()` is static and needs no installed package, so scripts always load. Components and resources register regardless.
  - With the package absent (or no window, no camera, a stale handle), functions do nothing and return `false`, zero, an invalid handle or `Entity.None`. A debug build warns once per native call site. Nothing throws.
  - `Resources.TryGet` reports an absent resource, and `Get` on one logs and returns an empty ref.
- **Tests:** each extension tests through FenrirScripting's harness, as physics does:
  - layouts are validated;
  - `new` equals `{}`;
  - a C# system loads a mesh, makes a material and sets a colour, then spawns a `MeshRenderer` and `Camera` that the renderer's `RendererProbe` sees on the null backend;
  - `ScreenPointToRay` agrees with C++;
  - `Sky` written from C# reaches the draw list;
  - fed key events show in `Keyboard.JustPressed` per phase and in an `EventReader`;
  - a missing package no-ops.
- **For ticket 33:** skeleton and animation follow these rules. Plain-data resources are mirrored, behaviour is native functions, assets are named by `AssetHandle<T>` and path, and variable-length data crosses as spans filled by the caller.
- **Later** (*Not yet specified*): resources defined in C# by script assemblies, and resource system parameters (`Res<T>`). Physics's `Gravity` as a mirror is the physics map's call. No new tickets.
