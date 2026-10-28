Status: ready-for-agent

# Extension set: the extensions' native rules behind a seam in Install, with a fake registrar for Rules tests

Origin: the third architecture review of `dotnet-scripting` (2026-10-28), candidate 4 (the extension set behind a seam in Install, rated Worth exploring), grilled to fourteen decisions in two rounds on 2026-10-28.

The vocabulary is the repo's: *scripting extension*, *extension state*, *native function*, *scripting contract*, *built-in extension* from `GLOSSARY.md`, and *extension set*, which this spec adds; *module*, *interface*, *seam*, *adapter*, *port*, *deep*, *locality* from the codebase-design skill. No ADR: nothing here is hard to reverse, ADR-0009's Install paragraph stays true, and the two sibling seams (#157, #195) got none.

## Problem statement

`Scripting::InstallImpl` (`packages/FenrirScripting/src/Scripting.cpp`, 842 lines) is a list of seven `StartupStage` lambdas. Stages three and four, with the `checkState` lambda above them, are pure rules over `ScriptingExtension` and `NativeFunction`, about 80 lines: every native function is named `Namespace.Type.Method` (its `BindingName` is non-empty); no name is declared twice, by one extension or by two; a native taking an extension state is listed by an extension carrying a state of its type; every extension state is kept alive for as long as its natives can be called; each native is registered, with its extension's state, through `NativeFunction::Register` before the contracts are shared (sharing loads them, and Iris binds their native fields then); and the shared assemblies are collected in extension order, the built-in extension's first. Every other piece of `State` sits behind a seam with Rules tests under `Tests/Rules/` (the registry, the change buffer, the script data, the generation lifecycle, the systems, the reload watcher). These rules have no seam and no C++ test of their own.

They are proven today by three cases in `Tests/AdmissionTests.cpp` (two extensions declaring one native; a stateful native in an extension carrying no state; one in an extension carrying another type), each a `Scripting::Install` over a `HeadlessApp` in the executable that needs the fixture assemblies built. Those three fail at stage three, before .NET starts, so the cost is the fixture and the app, not the runtime. One rule has no test at all: a registration Iris refuses ("the native function ... could not be registered"), which nothing can reach without a fake. `RegistryTableTests` covers only the registry's own duplicate-name rule, which is stage two's and already deep.

Reading "what a generation is to Iris" bounces across three adapters in two files: `IrisScriptAssemblyLoader` and `IrisSystemHostLoader` in `Scripting.cpp`, `IrisScriptAssembly` in `ScriptData.cpp` with its declaration in `ScriptData.hpp`.

Corrections to the review card, found before the grilling: `Scripting.cpp` is in 9 of the last 60 commits (32 is its whole history); `State` has 14 members; "another Scripting is already active" is the engine context's rule in `Bindings.cpp`, not an extension rule, and stays there; Iris's own doc says a registration may happen before or after the host starts, so the ordering rule is "before the contracts are shared", as stage four's comment already says.

## Solution

One module, `ExtensionSet`, behind one seam, `NativeRegistrar`, with the Iris adapter in `Scripting.cpp` and a fake in `Tests/Rules/`, and the six rules stated as Rules tests with no fixture and no HeadlessApp. Deletion test, honestly: delete the module and its 80 lines reappear in Install, so this is a move for locality and tests, not leverage; it earns its place by being the last set of rules in `State` without a seam, by making the refused-registration rule reachable, and by letting Install read as files, then the registry, then the set, then the host, then the lifecycle.

### `NativeRegistrar`: the seam

`NativeFunction::Make` bakes `Iris::RegisterInternalCall` into a private registrar lambda, so no fake can stand in below `NativeFunction::Register`. The port sits above it, in `src/NativeRegistrar.hpp`, `Fenrir::Detail`, naming nothing of Iris but `CallResult`:

```cpp
/** Where an extension set registers its native functions: each under its binding name, with its extension's state. */
class NativeRegistrar
{
  public:
    virtual ~NativeRegistrar() = default;
    [[nodiscard]] virtual Iris::CallResult<void> Register(const NativeFunction& function, void* state) = 0;
};
```

Two adapters stand at the seam:

- **`IrisNativeRegistrar`**, private to `Scripting.cpp` beside the two loaders (until ticket 02 gathers them): `Register` is `function.Register(state)`, the one line that reaches Iris. `NativeFunction::Register` is private with `friend class Scripting`; the header gains `friend class Detail::IrisNativeRegistrar`.
- **`FakeNativeRegistrar`**, in `Tests/Rules/FakeNativeRegistrar.hpp`: one knob, a binding name to refuse with a diagnostic; it records every `Register` in order as binding name, native name and state pointer. No other knobs.

### `ExtensionSet`: the module

`src/ExtensionSet.{hpp,cpp}`, `Fenrir::Detail`. A set exists only once its natives are registered, so the rule "registered before the contracts are shared" is in the type: Install reads the contract list from the set.

```cpp
/** Every scripting extension an Install was given, the built-in first, once its native functions have passed the
 *  naming, state and duplicate rules and been registered: it keeps the extension states alive and gives the
 *  contracts in extension order. */
class ExtensionSet
{
  public:
    /** Checks every rule over every extension first, with no side effect, then registers every native in extension
     *  order with its extension's state. InvalidArgument with today's message for a rule; the registrar's status, and
     *  "the native function 'X' of extension 'Y' could not be registered: <diagnostic>", for a refused registration. */
    [[nodiscard]] static Iris::CallResult<ExtensionSet> Install(std::vector<ScriptingExtension> extensions,
                                                                NativeRegistrar& registrar);
    /** The shared assemblies as listed, in extension order; Install prefixes the bridge directory. */
    [[nodiscard]] const std::vector<std::filesystem::path>& SharedAssemblies() const noexcept;
};
```

The set holds the extension states (`std::vector<std::shared_ptr<void>>`) and the shared-assembly list, and nothing else: the registry loop has already consumed the `ScriptingExtension` values when the set is built, and `ScriptingExtension::m_state` and `m_stateType` are read through `friend class Detail::ExtensionSet`. `ReadableName` (the demangler for the state-type diagnostics) moves into `ExtensionSet.cpp` with `checkState`.

What was registered before a refused registration stays registered: Iris cannot unregister, and the spec says so rather than pretending otherwise. A second Install re-registers the same functions under the same names, which Iris accepts, and re-points each native's state at the new Install's; that is why the set is built only after the active-slot check.

### Install, after

The order of stages does not change: files (the built-in extension inserted first), the registry's `AddExtension` loop over `config.extensions`, the active-slot check, then `ExtensionSet::Install(std::move(config.extensions), registrar)` into `State::extensions` (a `std::optional<Detail::ExtensionSet>`, replacing `extensionStates`, alive to State's end as the states are today: Shutdown never released them), then .NET with `state->extensions->SharedAssemblies()` prefixed by the bridge directory, then the lifecycle and the first load. The one visible change: a naming or state failure now happens after activation instead of before; Shutdown deactivates on failure either way. `StartupFailure`, the stage list and the loop stay in `Scripting.cpp`.

### The Iris adapters in one file (ticket 02)

`src/IrisAdapters.{hpp,cpp}` holds `IrisScriptAssemblyLoader` (with `Of`, which `Scripting::LoadedComponent` and the reconcile hook use), `IrisSystemHostLoader`, `IrisNativeRegistrar`, and `IrisScriptAssembly` moved from `ScriptData.{hpp,cpp}` with the two helpers only it uses (`ValidateTable`, `FingerprintOf`). `ScriptData.hpp` keeps the `ScriptAssembly` seam and loses the adapter. `Scripting.cpp` is left with `State`, `Scripting`'s members and Install. A pure move; the .NET tests are the proof.

### Out of scope

- The registry's four kinds (stage two): already behind the registry's seam with twelve Rules cases. The set stops at the natives and the contracts.
- The active-slot rule ("another Scripting is already active"): the engine context's, in `Bindings.cpp` and `EngineContext.hpp`, which candidate 5's spec touches. Nothing here edits those files.
- Iris's checks at share time (an unbound C# field, a mismatched signature): the two broken-extension cases in `AdmissionTests.cpp` stay.
- The file-existence stage, the lifecycle wiring and the phase scheduling: Install's.

## Decisions (from the grilling)

1. Build the seam in its minimal form rather than close the candidate; the honest size (80 lines, a move for locality and tests) is stated above.
2. The port sits above `NativeFunction::Register`, not below it: `NativeRegistrar::Register(const NativeFunction&, void*)`. A pure `Validate` function with registration left in Install was weighed and rejected as the extracted-for-testability shape.
3. The set owns the natives' rules, the states and the contract order only; the registry's four kinds stay where they are.
4. One step: a set exists only once registered, and the contract list is read from it.
5. The three Iris adapters, and the new one, gather in `IrisAdapters.{hpp,cpp}` as ticket 02.
6. Replace, don't layer: the three AdmissionTests cases the Rules test restates go; the two broken-extension cases and HostingTests' "second active install" stay.
7. `GLOSSARY.md` gains *extension set*, modelled on *system host*, landing with ticket 01.
8. No ADR.
9. Install's stage order stays as today; a rule failure moves to after activation.
10. The factory returns `Iris::CallResult<ExtensionSet>`; a refused registration's diagnostic is one joined string.
11. The set holds states and the shared-assembly list; `SharedAssemblies()` is its whole interface; the extension values are consumed.
12. The fake has one knob and records calls in order; the Rules test has the eleven cases below.
13. Ticket 02 moves `IrisScriptAssembly` with `ValidateTable` and `FingerprintOf`.
14. Ticket 01 is cut from `dotnet-scripting` now; 02 is stacked on 01's branch.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `NativeRegistrar.hpp`, `ExtensionSet.{hpp,cpp}`, the Iris adapter and the Install rewrite in `Scripting.cpp`, two friend lines in `ScriptingExtension.hpp`, the fake and the Rules test, three AdmissionTests cases deleted, the glossary entry | none |
| 02 | `IrisAdapters.{hpp,cpp}`: the four Iris adapters gathered, `IrisScriptAssembly` out of `ScriptData.{hpp,cpp}` | 01 |

Branches `extension-set-01-extension-set` cut from `dotnet-scripting` and targeting it, and `extension-set-02-iris-adapters` cut from 01's pushed branch and targeting it (both touch `Scripting.cpp`, so a stack). A seam with one adapter merged on its own would be a hypothetical seam, so the module, the adapter, the fake and the test land together in 01.

Overlap with the sibling specs of the same review: none with candidate 5 (`Bindings.cpp`, `EngineContext.hpp`, the built-in extension's layouts) or candidate 6 (Iris.Bindings' C#). Candidate 1+7 reshapes the test harnesses and may touch `packages/FenrirScripting/Tests/CMakeLists.txt`, which ticket 01 edits by two lines; whoever cuts second rebases.

## Done looks like

- `grep -n "is not named Namespace.Type.Method\|declared twice by extension\|declared by both extension\|carries one of type\|carries none\|could not be registered" packages/FenrirScripting/src/*.cpp` finds lines in `ExtensionSet.cpp` only.
- `grep -n "extensionStates\|checkState\|ReadableName\|cxxabi" packages/FenrirScripting/src/Scripting.cpp` finds nothing.
- `grep -n "Iris::" packages/FenrirScripting/src/NativeRegistrar.hpp packages/FenrirScripting/src/ExtensionSet.hpp` finds only `CallResult`.
- `grep -n "RegisterInternalCall\|\.Register(" packages/FenrirScripting/src/Scripting.cpp` finds one line, in `IrisNativeRegistrar` (after ticket 02, in `IrisAdapters.cpp`).
- `Tests/Rules/ExtensionSetTests.cpp` has eleven cases, each over the fake and no fixture: (1) a native not named `Namespace.Type.Method` fails, naming it and its extension, and nothing is registered; (2) a name declared twice by one extension fails naming the extension; (3) a name declared by two extensions fails naming both; (4) a stateful native in an extension carrying no state fails with "carries none"; (5) one in an extension carrying another type fails naming both types; (6) a stateless native in a stateful extension is registered with a null state; (7) every native of every extension is registered in extension order, under its binding name and native name, with its own extension's state pointer; (8) a refused registration fails with the function, the extension and the registrar's diagnostic, and what was registered before it stays registered; (9) the checks all run before any registration: a duplicate in the second extension registers nothing of the first; (10) the states outlive the extensions handed in and expire with the set; (11) shared assemblies come back in extension order.
- `AdmissionTests.cpp` no longer has "two extensions declaring one native function", "a native function taking a state its extension lacks" or "a native function taking a state of another type"; it keeps the two broken-extension cases; `HostingTests.cpp` keeps "a second active install is refused and explained".
- `GLOSSARY.md` defines *extension set*.
- After ticket 02: `grep -n "class Iris" packages/FenrirScripting/src/*.hpp packages/FenrirScripting/src/*.cpp` finds the four adapters in `IrisAdapters.hpp` only, and `grep -n "IrisScriptAssembly" packages/FenrirScripting/src/ScriptData.hpp packages/FenrirScripting/src/ScriptData.cpp` finds nothing.
- The msvc-x64-debug preset builds; `FenrirScriptingRules` and `FenrirScripting` pass; the other presets are CI's.
