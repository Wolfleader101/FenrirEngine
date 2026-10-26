# 02: Mark the 43 pass-throughs and trim the five facades to what a description cannot say

**What to build:** Each of the 43 pass-through natives gets its `@facade` mark, the five hand-written facade classes become `partial` and lose those members, and every binding is regenerated (spec: "The port", decisions 1, 10, 11, 12).

- **Marks** (`@facade Class.Method` in each native's doc comment, in these sources):
  - `packages/FenrirWindow/Scripting/src/WindowScripting.cpp` (11, all `Window.<same name>`): `Exists`, `ContentScale`, `IsMinimised`, `IsFocused`, `GetCursorMode`, `SetCursorMode`, `SetCursorShape`, `SetTitle`, `GetClipboard`, `SetClipboard`, `RequestClose`.
  - `packages/FenrirInput/Scripting/src/InputScripting.cpp` (15): `KeyIsDown`, `ScancodeIsDown` → `Keyboard.IsDown`; `KeyJustPressed`, `ScancodeJustPressed` → `Keyboard.JustPressed`; `KeyJustReleased`, `ScancodeJustReleased` → `Keyboard.JustReleased`; `MouseIsDown` → `Mouse.IsDown`, `MouseJustPressed` → `Mouse.JustPressed`, `MouseJustReleased` → `Mouse.JustReleased`; `GamepadsConnected` → `Gamepads.Connected`, `GamepadIsConnected` → `Gamepads.IsConnected`, `GamepadIsDown` → `Gamepads.IsDown`, `GamepadJustPressed` → `Gamepads.JustPressed`, `GamepadJustReleased` → `Gamepads.JustReleased`, `GamepadAxisOf` → `Gamepads.Axis`.
  - `packages/FenrirPhysics/Scripting/src/PhysicsScripting.cpp` (7, all `PhysicsWorld.<same name>`): `AddForce`, `AddImpulse`, `AddTorque`, `AddAngularImpulse` (the `in Vector3` becomes by value under the rule), `ContactsOf`, `IsSleeping`, `Bounds`.
  - `packages/FenrirDebugDraw/Scripting/src/DebugDrawScripting.cpp` (5): `ShowGroup`, `HideGroup`, `IsGroupShown`, `Clear` → `DebugDraw.<same name>`; `ClearGroup` → `DebugDraw.Clear`.
  - `packages/FenrirRenderer/Scripting/src/RendererScripting.cpp` (5, all `Renderer.<same name>`): `ScreenPointToRay`, `WorldToScreenPoint`, `CameraAt` (`in Vector2`/`in Vector3` by value under the rule), `GetWorldBounds`, `Stats`.
- **Docs moved into the C++ comments**, since the public doc is now the native's: the facade's wording for `GetClipboard` (as much as fits is written; zero with no text), `RequestClose` (the event sent and the app stopping), `Gamepads.Connected` (the oldest are written when more are connected than fit), `ContactsOf` (triggers are not contacts; a character's contacts), `IsSleeping` (a child collider's entity stands for its body), `Bounds` (`bounds` is default with no body), and `Renderer`'s `ScreenPointToRay`, `WorldToScreenPoint`, `CameraAt`, `GetWorldBounds` and `Stats` (merge each facade `<summary>` and `<returns>` with the native's; keep what either says). `@return` added to `AddForce`, `AddImpulse`, `AddTorque`, `AddAngularImpulse` ("Whether the ... was queued: false with no physics world or no body"). The "(DebugDraw.ShowGroup)" style cross-reference removed from the five debug draw marks' docs. `<paramref name="x"/>` becomes `` `x` ``, `<see cref="T"/>` becomes `` `T` ``.
- **Hand-written classes** (`Window.cs`, `Devices.cs`, `PhysicsWorld.cs`, `DebugDraw.cs`, `Renderer.cs` under `packages/*/Managed/Fenrir.*/`): each facade class becomes `public static partial class`, its header comment says the pass-throughs are generated beside it (`Generated/<Class>.cs`) and this part holds what a description cannot say, and the 43 members are deleted. `Window.Main`, `DebugDraw.Forever`, the properties, `Size`, `PixelSize`, `GetJointState`, `Depenetrate`, the five with `QueryFilter filter = default`, `SetDebugDraw`, `CreateCollisionMesh`, `ReleaseCollisionMesh`, the 26 shape and text calls, `SpawnModel` and the class summaries stay.
- **Regeneration**: `fenrir-describe-api` rewrites the five `*.natives.json` with `facade`; `fenrir-generate-bindings` writes `Fenrir.Window/Generated/Window.cs`, `Fenrir.Input/Generated/{Keyboard,Mouse,Gamepads}.cs`, `Fenrir.Physics/Generated/PhysicsWorld.cs`, `Fenrir.DebugDraw/Generated/DebugDraw.cs`, `Fenrir.Renderer/Generated/Renderer.cs`, and the declarations' docs change where the comments did. The assets binding is untouched: nothing in `AssetsScripting.cpp` is a pass-through.
- **Proof**: every deleted member has a generated one of the same name, parameters (by value where the declaration says `in`) and result, so the five contracts, the examples' script assemblies and the fixtures (`packages/{FenrirWindow,FenrirInput,FenrirPhysics,FenrirDebugDraw,FenrirRenderer}/Scripting/Tests/Fixture/*.cs`) build unchanged and their scripting tests pass. No new test.

**Blocked by:** 01

**Touches:** `packages/FenrirWindow/Scripting/`, `packages/FenrirWindow/Managed/`, `packages/FenrirInput/Scripting/`, `packages/FenrirInput/Managed/`, `packages/FenrirPhysics/Scripting/`, `packages/FenrirPhysics/Managed/`, `packages/FenrirDebugDraw/Scripting/`, `packages/FenrirDebugDraw/Managed/`, `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`

**Status:** ready-for-agent

- [ ] `grep -c "@facade"` over the five sources finds 11, 15, 7, 5, 5; the five generated facade files exist; no generated public method takes `in`.
- [ ] The five hand-written facade files hold no member that only forwards to a native.
- [ ] `fenrir-describe-api` and `fenrir-generate-bindings` are no-ops after the commit (CI's api-descriptions check).
- [ ] The msvc-x64-debug preset builds and `ctest -LE gl` passes locally, the packages' scripting tests among them; the other presets are CI's.

## Comments

Filed from the facade-passthroughs grilling (spec decisions 1, 10, 11, 12). Cut from ticket 01's branch and targeting it until it merges.
