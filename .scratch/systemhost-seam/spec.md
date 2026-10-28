Status: ready-for-agent

# SystemHost seam: the systems runner behind a seam, with its rules as Rules tests

Origin: the second architecture review of `dotnet-scripting` (2026-10-27), candidate 4 (the systems runner behind a SystemHost seam, rated Worth exploring), grilled to nine decisions in two rounds on 2026-10-28.

The vocabulary is the repo's: *generation*, *script assembly*, *system*, *structural change* from `GLOSSARY.md`, and *system host*, which this spec adds; *module*, *interface*, *seam*, *adapter*, *deep*, *locality* from the codebase-design skill. No ADR: nothing here is hard to reverse, and the generation-lifecycle seam of the same shape (#157) got none.

## Problem statement

`Scripting::State` (`packages/FenrirScripting/src/Scripting.cpp`) runs the script's systems through six members (`runner`, `runnerRun`, `runnerDestroy`, `mainThread`, `phaseCalls`, `phaseMask`) and three functions, `StartSystems`, `RunPhase` and `StopSystems`, 97 lines. `StartSystems` finds the generated type by name, binds four methods by name, reads the phase mask and runs `OnCreate`; `RunPhase` gates on the mask and the main thread and makes one transition per phase; `StopSystems` says goodbye. Every other piece of `State` sits behind a seam with Rules tests under `Tests/Rules/` (the registry, the change buffer, the script data, the generation lifecycle through `ComponentLoader`, the reload watcher). This one has no seam and no C++ test.

What C++ owns here is small, and that is the honest size of the win. The order of systems within a phase, the playback of structural changes after each system and the dropping of a system that throws are C#'s rules, in `SystemRunner` (`Managed/Fenrir.Scripting/Systems.cs`), and `Tests/ScriptingTests.cpp` pins them through the real runtime ("systems run in phase and order, with changes applied after each"). The C++ rules are: a script with no system host has no systems; a host that cannot be found or bound, whose mask cannot be read or whose `OnCreate` fails, fails the load; a phase whose mask bit is clear costs nothing; a phase asked for off the main thread is skipped with one error; a failing run is logged and the next phase still runs; `OnDestroy` runs once at stop and nothing runs after it. Of these only the mask gating has a test, indirectly, through the phase-call count. The two failure rules cannot be reached through .NET without a corrupt assembly. The main-thread rule cannot be tested as written: the off-thread path hits `assert(false)` before the log and the skip, so a Debug test run would abort.

The names of the contract between the engine and the generated host (`Fenrir.Scripting.Generated.SystemHost`, `PhaseMask`, `OnCreate`, `Run`, `OnDestroy`) are spelled in `Scripting.cpp` and again in `Managed/Fenrir.Scripting.Generators/SystemGenerator.cs`, with nothing naming one as the owner. Drift is caught, by the .NET fixture failing to start, but a reader finds the contract in neither place.

ADR-0039's extension stages are not built yet; when they are, they land in this runner.

## Solution

One module, `ScriptSystems`, behind one seam, `SystemHost`, with the Iris adapter in `Scripting.cpp` and a fake in `Tests/Rules/`, and the C++ rules stated as Rules tests with no .NET. Deletion test, honestly: delete the module and its 97 lines reappear in `State`, so this is a move for locality and tests, not leverage; it earns its place by being the last piece of `State` without a seam and the place extension stages will grow.

### `SystemHost`: the seam

`src/SystemHost.hpp`, `Fenrir::Detail`, naming nothing of Iris but `CallResult`:

```cpp
/** The script's one systems object, found in a generation and bound: what the engine calls once per phase. */
class SystemHost
{
  public:
    virtual ~SystemHost() = default;
    [[nodiscard]] virtual Iris::CallResult<std::int32_t> PhaseMask() = 0;
    [[nodiscard]] virtual Iris::CallResult<void> OnCreate() = 0;
    [[nodiscard]] virtual Iris::CallResult<void> Run(std::int32_t phase) = 0;
    [[nodiscard]] virtual Iris::CallResult<void> OnDestroy() = 0;
};

/** Where a generation's system host comes from. NotFound means the script defines no systems. */
class SystemHostLoader
{
  public:
    virtual ~SystemHostLoader() = default;
    [[nodiscard]] virtual Iris::CallResult<std::unique_ptr<SystemHost>> Find(LoadedComponent& generation) = 0;
};
```

Two adapters stand at the seam:

- **`IrisSystemHostLoader`**, private to `Scripting.cpp`, beside `IrisComponentLoader`: `Find` takes the Iris component through `IrisComponentLoader::Of`, does `FindType`, the four `FindInstanceMethod` lookups and `CreateObject`, and answers `NotFound` when the type is absent, the host when all four bind, and otherwise the status with "The script's system host is missing a method: ..." as today. Its `SystemHost` holds the `Iris::Object` and the two bound methods and forwards each call. The method lookup, and so the missing-method diagnostic, is the adapter's and is not reachable by the fake; the spec says so rather than pushing the seam down to Iris's type level, which would be a shallow re-wrap of Iris.
- **`FakeSystemHostLoader`**, in `Tests/Rules/FakeSystemHost.hpp`: told whether `Find` answers no host, a failure, or a host; the mask the host answers; which of `PhaseMask`, `OnCreate`, `Run` and `OnDestroy` fail. It records every call in order (`Find`, `PhaseMask`, `OnCreate`, `Run 2`, `OnDestroy`) with the thread that made it. No other knobs.

### `SystemHostNames`: one owner for the five names

`src/SystemHostNames.hpp` holds five `constexpr std::string_view` constants (the type's full name and the four method names). The Iris adapter spells the contract from them and nothing else in C++ spells a literal. `SystemGenerator.cs` gains a comment at the emitted class naming that header as the contract's owner; the generator keeps its literals, since five strings earn no generated file, and the .NET fixture failing to start stays the drift check.

### `ScriptSystems`: the module

`src/ScriptSystems.{hpp,cpp}`, constructed once with a `SystemHostLoader&` and an `ILogger*`, both borrowed as the lifecycle's are:

```cpp
class ScriptSystems
{
  public:
    using Result = Iris::CallResult<void>;
    ScriptSystems(SystemHostLoader& loader, ILogger* logger);
    /** Finds the generation's host, records the calling thread as the main thread, reads the mask and runs OnCreate.
     *  Success and nothing held when the script defines no systems. A failure holds nothing and is the load's failure. */
    [[nodiscard]] Result Start(LoadedComponent& generation);
    /** One transition for the phase: nothing when no host is held or the mask bit is clear; off the main thread one
     *  error and nothing invoked; a failing run logged. */
    void RunPhase(int phase);
    /** OnDestroy once if a host is held (a failure logged), then nothing held. */
    void Stop();
    [[nodiscard]] bool HasSystems() const noexcept;
    [[nodiscard]] std::size_t PhaseCalls() const noexcept;
};
```

`State` keeps one `ScriptSystems systems` member, declared after `loader` and before `generations`, and its `start` and `stop` hooks become one-liners; `ReadDiagnostics` reads `systems.PhaseCalls()`. The six members and the three functions go. The `contextActive` guard in today's `StopSystems` goes with them: Install activates the context before any generation loads and Shutdown unloads the generation before it deactivates the context, so the guard never fired while a host was held. The phase scheduling loop stays in Install, since the App is Install's. The `assert(false && "Scripts must only run on the main thread")` goes: the rule is the log and the skip, on every build, and the Rules test pins it.

### Out of scope

- The order of systems within a phase, playback after each system and a throwing system dropped: C#'s rules in `SystemRunner`, pinned by `ScriptingTests.cpp`. They stay there; no C++ test restates them.
- The missing-method diagnostic: the Iris adapter's, reachable only through a real assembly.
- Extension stages (ADR-0039): not built here; the seam is where they will go.
- `Tests/ScriptingTests.cpp`, `Tests/NativeCallHarness.hpp` and `src/ComponentLoader.hpp`'s name: the test-support spec's.

## Decisions (from the grilling)

1. Build the seam in its minimal form rather than close the candidate: the last piece of `State` without a seam, two failure rules unreachable any other way, and the place extension stages will grow. Closing was weighed and the move's honest size (97 lines, the risky rules C#'s) is stated above.
2. The five names live in one C++ header the Iris adapter uses, `SystemHostNames.hpp`; the generator's comment names it; no generated file, no new check.
3. The assert goes; the main-thread rule is one error logged and the phase skipped, on every build.
4. The seam is two types, `SystemHost` and `SystemHostLoader`, the shape of `LoadedComponent` and `ComponentLoader`; `NotFound` means no systems; the method lookup is the adapter's.
5. `ScriptSystems` has `Start`, `RunPhase`, `Stop`, `HasSystems` and `PhaseCalls`; the `contextActive` guard goes; the scheduling loop stays in Install.
6. The Rules tests pin seven rules (below) and nothing C# owns.
7. The fake has exactly the knobs those rules need and records calls with their thread.
8. No ADR; `GLOSSARY.md` gains *system host*, landing with the ticket.
9. One ticket, cut from `dotnet-scripting` once the test-support tickets touching `Tests/CMakeLists.txt` and `src/ComponentLoader.hpp` have merged.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `SystemHost.hpp`, `SystemHostNames.hpp`, `ScriptSystems.{hpp,cpp}`, the Iris adapter and one-line hooks in `Scripting.cpp`, the generator comment, the fake and the Rules test, the glossary entry | none in this folder; the test-support tickets that touch `packages/FenrirScripting/Tests/CMakeLists.txt` and `src/ComponentLoader.hpp` (the seam header this one includes) must have merged first |

Branch `systemhost-seam-01-script-systems`, cut from `dotnet-scripting` after those merge, targeting it. A seam with one adapter merged on its own would be a hypothetical seam, so the module, the adapter, the fake and the test land together.

## Done looks like

- `grep -n "Fenrir.Scripting.Generated.SystemHost\|\"PhaseMask\"\|\"OnCreate\"\|\"OnDestroy\"\|\"Run\"" packages/FenrirScripting/src/*.cpp packages/FenrirScripting/src/*.hpp` finds lines in `SystemHostNames.hpp` only; `grep -n "SystemHostNames" packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/SystemGenerator.cs` finds the comment.
- `grep -n "assert" packages/FenrirScripting/src/Scripting.cpp` finds nothing; `grep -n "runner\|mainThread\|phaseMask" packages/FenrirScripting/src/Scripting.cpp` finds nothing but the adapter.
- `grep -n "Iris::" packages/FenrirScripting/src/SystemHost.hpp packages/FenrirScripting/src/ScriptSystems.hpp` finds only `CallResult`.
- `Tests/Rules/ScriptSystemsTests.cpp` has a case for each of the seven rules: no host is success with nothing held; a `Find`, `PhaseMask` or `OnCreate` failure fails `Start` with its diagnostic and holds nothing; a clear mask bit invokes nothing and a set one invokes `Run` once with that phase, counted; a phase from another thread invokes nothing and logs one error; a failing `Run` is logged once and the next phase still runs; `Stop` runs `OnDestroy` once, a second `Stop` nothing, a phase after `Stop` nothing; `Start` after `Stop` binds the new host.
- `GLOSSARY.md` defines *system host*.
- The msvc-x64-debug preset builds; `FenrirScriptingRules` and `FenrirScripting` (the .NET tests, "systems run in phase and order" included) pass; the other presets are CI's.
