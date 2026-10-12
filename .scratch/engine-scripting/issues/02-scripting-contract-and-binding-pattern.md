# 02: Fenrir.Scripting contract, Entity, and the binding pattern

**What to build:** The shared contract assembly `Fenrir.Scripting`, registered as an Iris shared assembly and never reloaded (ADR-0004). It establishes the one pattern every engine API uses: a C++ function registered with `RegisterInternalCall`, plus a friendly static C# wrapper in the contract that hides pointers, UTF-8 and lengths from authors (review-18 E4). This ticket proves the pattern with `Log` (Info/Warn/Error to `ILogger`), `Time` (delta time, tick rate) and the `Entity` value type (`readonly struct Entity { uint Value; }`, the full `entt::entity` value including version bits, with `IsValid` against the active scene).

**Blocked by:** 01

**Status:** resolved

- [x] A fixture script assembly referencing `Fenrir.Scripting` logs through `Log` and reads `Time`, and the test target observes both.
- [x] `Entity.IsValid` is false for a destroyed entity, and false again after its ID is reused (the version differs).
- [x] Contract types have one identity across generations (they come from the shared assembly).
- [x] The binding pattern is documented in the package README as the recipe for adding an engine API.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Binding native code; Model).

### Resolution

- **Iris gap found and fixed first (Iris ticket 24).** Iris bound internal calls only in the loaded component, never in a shared assembly, so a shared contract's native calls were never bound. Ticket 24 makes sharing bind them and fail with a named field if a registration is missing. `Host::Initialize` now attaches registered internal calls before it shares assemblies.
- **Contract:** `packages/FenrirScripting/Managed/Fenrir.Scripting` (version 0.1.0, net10.0): `Entity`, `Log`, `Time`, an internal `Native` class of function pointers, and `[assembly: DisableRuntimeMarshalling]`. CMake builds it (`FenrirScriptingContract`), and `fenrir_deploy_scripting(<target>)` puts it, nethost and the bridge beside an executable. `Scripting::Install` always shares it.
- **Entity:** `readonly struct Entity { uint Value }`, the full EnTT value including version bits, and `IsValid` asks the active scene. A destroyed entity is invalid, and stays invalid after its slot is reused (the new entity has a new version; the test asserts the slot really was reused).
- **Log:** `Log.Info/Warn/Error` go to the engine logger as UTF-8 plus a length. The text is passed as data, never as a format string, so braces are safe. There is no length limit (a 2000-character message is tested) and null logs an empty message.
- **Time:** `Time.DeltaTime` and `Time.TickRate` are `float`s read from the App's clock when asked.
- **Binding pattern:** documented as a three-piece recipe in `packages/FenrirScripting/README.md`.
- **Active context:** internal calls are plain function pointers, so the App they serve is process-wide state, one `Scripting` at a time. A second concurrent `Install` is refused with an error. Calls from the C# side return safe defaults when no context is active.
- **Tests (14 cases, Debug and Release, MSVC and clang):** the fixture script assembly (referencing the contract with `Private=false`) logs at every level, reads the clock from inside a running frame, and checks entity validity; a test proves the contract is in the default load context and is the same assembly across two installs; and a missing contract is logged with the app still running. `Scripting::GetComponent()` is the hook the tests (and later the runner) call scripts through.
