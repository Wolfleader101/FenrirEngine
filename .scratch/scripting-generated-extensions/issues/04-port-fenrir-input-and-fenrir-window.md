# 04: Port `Fenrir.Input` and `Fenrir.Window` onto the generated pipeline

**What to build:** Input's and Window's mirrors, mappings, native declarations and registrations are generated; their facades are hand-written over typed natives; `InputClaims` crosses as a described copy with a hand-written refreshing registration (spec: "Each port"; ADR-0043, ADR-0044).

- **FenrirInput selection** (`packages/FenrirInput/api/FenrirInput.api.toml`, headers `InputTypes.hpp`, `InputEvents.hpp`): the nine events as `event` (`TextEvent`'s `char32_t` is a `u32` now, so `Detail::TextEventLayout` goes); `WindowId` and `GamepadId` as `shared-struct` (one field each: wrappers). The enums (`Key`, `Scancode`, `KeyModifiers`, `MouseButton`, `GamepadButton`, `GamepadAxis`) are described because the events hold them; their `Count` enumerators are mirrored (today's hand-written enums leave them out: the ticket either keeps them, documented as sizes, or asks describe-api to honour a `@hidden` doc command, which is the one generator change this ticket may make).
- **Binding-owned selection** (`packages/FenrirInput/Scripting/api/FenrirInputScripting.api.toml`, header `src/ScriptInputClaims.hpp`): `Detail::ScriptInputClaims` as `app-resource`, with `ScriptWindowClaims`, `WindowInputClaims` and `CaretRect` described through it. `[names]` registers it as `Fenrir.Input.InputClaims`; it is in `read-only` and `by-hand`; the hand-written C# indexer and `For(window)` stay as a partial part.
- **FenrirInput binding** (`FenrirInput.binding.toml`): `readonly-events = true`; `[uses]` the math binding; `[natives] sources = ["../src/InputScripting.cpp"]`, class `InputNative`; `[registration]` into `src/Generated/FenrirInputRegistration.cpp`; `[tests]` into the extension's test target. `Iris::WrapperOf<WindowId>` and `<GamepadId>` specialised in `InputMappings.hpp`, which keeps nothing else (`[uses]` includes the generated header).
- **Input natives:** each takes `Key`, `Scancode`, `MouseButton`, `GamepadId`, `GamepadButton`, `GamepadAxis` as the C++ API does; `KeyModifiers()` returns the enum; outputs carry `@param[out]`; the warn-once record moves to an `InputScripting` state class (`SetState`). `Input::ScriptingExtension()` becomes `name`, `SetState`, `sharedAssemblies`, `Generated::RegisterInput(extension)`, then `extension.resources.push_back(InputClaimsResource())`.
- **FenrirWindow selection** (`packages/FenrirWindow/api/FenrirWindow.api.toml`, header `Window.hpp` or where the events and `WindowSize`, `CursorMode`, `CursorShape` live): the two events as `event`, `WindowSize` as `shared-struct`. Binding: `[uses]` input's and math's bindings (for `WindowId`); natives take `WindowId`, `CursorMode`, `CursorShape` and `std::string_view`; `GetClipboard`'s `char16_t*` + capacity is already a span. State class `WindowScripting`.
- **Facades:** `Keyboard`, `Mouse`, `Gamepads`, `Window` rewritten over the generated classes: casts and `.Value` gone, overloads, properties and `Window.Main` kept, remarks kept. `WindowId` and `GamepadId` keep equality and `ToString` as partial parts.
- **Deleted:** hand-written `Events.cs`, `Types.cs` enums and ids as fields, `InputClaims.cs` fields, `InputNative`/`WindowNative` declarations, `InputMappings.hpp`'s `IRIS_FIELD` mappings, `Utf16.hpp` only if the ticket finds it unused, the `extension.functions`/`.events` lists.

**Blocked by:** 03 (`Fenrir.Math` through a math binding, and physics's `Aabb` becomes `Bounds`)

**Touches:** `packages/FenrirInput/api/`, `packages/FenrirInput/Managed/`, `packages/FenrirInput/Scripting/`, `packages/FenrirWindow/api/`, `packages/FenrirWindow/Managed/`, `packages/FenrirWindow/Scripting/`

**Status:** resolved

- [x] `fenrir-describe-api` and `fenrir-generate-bindings` leave the tree clean; no `IRIS_FIELD`, `[NativeFunction]` or `NativeFunction::Make` remains under either package.
- [x] The existing input and window extension tests pass unchanged in behaviour (fed keys per phase, mouse, gamepads, `InputClaims`, hidden-window clipboard, no-op rules with one debug warning per native call site through the state's record).
- [x] The generated defaults and natives tests run in both test targets; `TextEvent` arrives with its codepoint through the described event.
- [x] A script reads `Keyboard.JustPressed(Key.Space)`, `Window.Size(Window.Main)` and `Resources.TryGetReadOnly<InputClaims>` exactly as before.

## Comments

**The `Count` enumerators are kept (settled).** The generated enums carry C++'s `Count`, so each C# enum names exactly
C++'s values. `Count` is documented in `FenrirInput/InputTypes.hpp` as a size, not an input; that is a doc-only change
outside the Touches line, and its text becomes the C# doc. `Scripting/Tests/Shapes.cpp` now expects `Count` among each
enum's members. No `@hidden` command was added.

**The generator change this ticket makes is a selection role `enum` (describe-api), not `@hidden`.** Window's natives
take `CursorMode` and `CursorShape` by value, but no field holds either, so nothing described them, and describe-api
refused an enum in a selection. A selection may now name an enum as `enum`: it is described exactly as a field-reached
enum is, with no role in the output. Anything else on an enum, or `enum` on a non-enum, fails, and each has a test case.
The describe-api fixture gains `Heading`, selected this way, and so does Iris.Bindings' copy of it.

**A type may be described in two descriptions when alike (Iris.Bindings, outside the Touches line).** FenrirInput's
binding binds two descriptions: the package's, and the library's own, of `ScriptInputClaims`. The second nests
FenrirInput's `WindowId`, `WindowInputClaims` and `CaretRect`. Iris.Bindings refused any type described twice that was
not a bound type. Now a struct, tagged union or enum two descriptions describe alike (the same fields, offsets, kinds,
defaults and values; docs may differ) is kept once, with the role it is selected with. Described differently, or
selected with two roles, it fails. Each case has a test.

**The SDK build check no longer hangs (Iris.Bindings tests).** `TheMirrorsBuildAgainstTheScriptingSdkWithNoDiagnostic`
runs `dotnet build` and reads its output to the end. A reused MSBuild node outlived the build while holding the
redirected pipes open, so the read waited forever (this is the hang ticket 02 saw once). The build now passes
`-nodeReuse:false`.

**`ScriptInputClaims` changed shape (binding-owned, inside Touches).**
- It moved to `Scripting/include/FenrirInputScripting/` because the generated mappings, which include it, are public:
  window's binding includes them, and so will the renderer's.
- Its `std::array` became a C array, since describe-api would describe the library's array internals.
- Its fields became `entries`, `entryCount` and `gamepadClaimed`. Their C# mirrors (`Entries`, a nested `EntriesArray`,
  `EntryCount`, `GamepadClaimed`) are private and clash with none of `InputClaims`'s hand-written `WindowCount`,
  `Gamepad`, indexer and `For`.
- `CaretRect`, `WindowInputClaims` and `WindowClaimsEntry` stay readonly (`readonly-structs`). Read-only mirrors gain a
  `public T()` that runs their initialisers, which is new C# surface, all zero as before.

**`InputClaimsResource` lives in its own source** (`src/InputClaimsResource.cpp`). It needs the generated mappings, and
a natives source cannot include them: libclang cannot evaluate `BoundMappings.hpp`'s constexpr `offsetof`s (physics's
natives source includes no mappings either). Its registration takes the extension's state, so its "more windows than
C# can read" warning goes through the state's record.

**Warn once is per extension now.** Each extension's state keeps its own record. Since `Input::ScriptingExtension()` and
`Window::ScriptingExtension()` make a new state each time, a warning is once per call site per Scripting, not per
process. The facades' remarks still say "once per process", which is true for an app with one Scripting.

**Native names.** The C++ names are kept: `KeyModifiersOf` and `GamepadAxisOf`, so C#'s internal `InputNative` names
those two that way; the facades are unchanged. Window's natives are `Fenrir::WindowNatives::*`.

**Outside the Touches line, all small:**
- `fenrir_describe_api` lines in `FenrirInput/CMakeLists.txt` and `FenrirWindow/CMakeLists.txt`, where each package
  describes itself, as FenrirECS and FenrirPhysics do.
- `FenrirRenderer/Scripting/include/FenrirRendererScripting/RendererMappings.hpp` includes the generated input mappings,
  since `WindowId`'s mapping left `InputMappings.hpp`. Ticket 06 replaces that file.
- The root `README.md` and `FenrirScripting/README.md` still list input and window among the extensions "still written
  by hand". 05 and 07 edit the same sentences in parallel, so the port that closes the spec updates them once.

**Kept:** `Utf16.hpp` (`GetClipboard` converts to UTF-16). **Added:** `Scripting/Tests/WindowScriptingApp.hpp`, the
harness window's generated tests run in (no window, so the natives test sees nothing to act on).
