# 01: ScriptSystems behind the SystemHost seam, with its rules as Rules tests

**What to build:** the systems runner leaves `Scripting::State` for a `ScriptSystems` module behind a `SystemHost` seam with an Iris adapter and a fake, the five contract names get one owner, the assert goes, and the seven C++ rules become Rules tests with no .NET (spec: "Solution").

- **`packages/FenrirScripting/src/SystemHost.hpp`** (new): `Detail::SystemHost` (`PhaseMask`, `OnCreate`, `Run(phase)`, `OnDestroy`, each returning an `Iris::CallResult`) and `Detail::SystemHostLoader` (`Find(LoadedComponent&)` returning `CallResult<std::unique_ptr<SystemHost>>`; `NotFound` means the script defines no systems). Includes `ComponentLoader.hpp` for `LoadedComponent` (under whatever name the test-support spec has given that header by then). Names nothing of Iris but `CallResult`.
- **`packages/FenrirScripting/src/SystemHostNames.hpp`** (new): five `constexpr std::string_view` constants: the generated type's full name `Fenrir.Scripting.Generated.SystemHost` and `PhaseMask`, `OnCreate`, `Run`, `OnDestroy`, with a comment saying the generator mirrors them.
- **`packages/FenrirScripting/src/ScriptSystems.{hpp,cpp}`** (new): the module as the spec's listing: constructed with `SystemHostLoader&` and `ILogger*` (borrowed); `Start(LoadedComponent&)` finds the host, records `std::this_thread::get_id()` as the main thread, reads the mask, runs `OnCreate`, holds nothing on any failure and returns it with today's prefixes ("The script's systems could not be created/listed/start: ..."); `RunPhase(int)` does nothing when no host is held or the mask bit is clear, logs one error and invokes nothing off the main thread, invokes `Run` once otherwise and logs a failure; `Stop()` runs `OnDestroy` once if a host is held (failure logged), then holds nothing with mask zero; `HasSystems()`, `PhaseCalls()`.
- **`packages/FenrirScripting/src/Scripting.cpp`**: `IrisSystemHostLoader` beside `IrisComponentLoader`, doing `FindType`, the four `FindInstanceMethod` lookups and `CreateObject` from `SystemHostNames.hpp`, answering `NotFound` for no type, "The script's system host could not be read: ..." / "is missing a method: ..." as today; its `SystemHost` holds the `Iris::Object` and the bound methods. `State` gains `std::optional<IrisSystemHostLoader> hostLoader` and `ScriptSystems systems` (declared after `loader`, before `generations`; both borrow what is declared before them), loses `runner`, `runnerRun`, `runnerDestroy`, `mainThread`, `phaseCalls`, `phaseMask`, `StartSystems`, `RunPhase`, `StopSystems`; the hooks become `systems.Start(loaded)` and `systems.Stop()`; the scheduled phase lambdas call `systems.RunPhase(phase)`; `Shutdown`'s `runner.has_value()` becomes `systems.HasSystems()`; `ReadDiagnostics` reads `systems.PhaseCalls()`. The `contextActive` guard around `OnDestroy` and the `assert` go; `<cassert>` goes if nothing else uses it.
- **`packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/SystemGenerator.cs`**: a comment at the emitted `SystemHost` class naming `src/SystemHostNames.hpp` as the contract's owner. No code change.
- **`packages/FenrirScripting/Tests/Rules/FakeSystemHost.hpp`** (new): `RulesTest::FakeSystemHostLoader` with the spec's knobs (no host / `Find` fails / a host; the mask; which of the four methods fail) recording every call in order with the calling thread's id.
- **`packages/FenrirScripting/Tests/Rules/ScriptSystemsTests.cpp`** (new): one case per rule in the spec's "Done looks like", over the fake and `HeadlessApp`'s logger. The off-thread case runs `RunPhase` on a `std::thread` and joins it.
- **`packages/FenrirScripting/Tests/CMakeLists.txt`**: the two Rules files added to `FenrirScriptingRulesTests` (or to whatever the test-support spec has made of that target).
- **`GLOSSARY.md`**: *system host* after *System*.

**Blocked by:** none

**Touches:** `packages/FenrirScripting/src/SystemHost.hpp`, `packages/FenrirScripting/src/SystemHostNames.hpp`, `packages/FenrirScripting/src/ScriptSystems.hpp`, `packages/FenrirScripting/src/ScriptSystems.cpp`, `packages/FenrirScripting/src/Scripting.cpp`, `packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/SystemGenerator.cs`, `packages/FenrirScripting/Tests/Rules/`, `packages/FenrirScripting/Tests/CMakeLists.txt`, `GLOSSARY.md`

**Status:** resolved

- [x] Not cut until the test-support tickets touching `packages/FenrirScripting/Tests/CMakeLists.txt` and `src/ComponentLoader.hpp` have merged into `dotnet-scripting`; then cut from `dotnet-scripting` and target it.
- [x] The spec's "Done looks like" greps hold; the seven Rules cases pass in under a second each.
- [x] The msvc-x64-debug preset builds; `FenrirScriptingRules` and `FenrirScripting` pass, "systems run in phase and order" and "a phase does nothing after Shutdown" included.

## Comments

Filed from the SystemHost seam grilling (spec decisions 1 to 9).
