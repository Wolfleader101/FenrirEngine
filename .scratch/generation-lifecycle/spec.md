Status: ready-for-agent

# Generation lifecycle: a loader seam under it, and one module for a generation's script data

Origin: the architecture review of `dotnet-scripting` against `main` (2026-10-26), candidate 2 (the lifecycle's riskiest rules sit behind a raw `Iris::Host*` and are tested only end to end through .NET, rated Strong) and candidate 4 (what a generation registers and what survives a reload is spread over five files, with a partial-registration hole when a later Keep pass fails, rated Worth exploring), grilled to nine decisions in one round. The review's candidate 8 (a pure `ValidateExtensions` for Install, rated speculative) was settled as out of scope. Candidate 5 (the registry's three tables) stacks after this work and is not touched.

The vocabulary is the repo's: *generation*, *script assembly*, *script report*, *Iris component*, *release*, *collection*, *extension state* from `GLOSSARY.md`; *module*, *interface*, *seam*, *adapter*, *deep*, *shallow*, *locality*, *leverage* from the codebase-design skill. No ADR: nothing here is hard to reverse; the glossary gains two terms with ticket 02.

## Problem statement

`GenerationLifecycle` (`packages/FenrirScripting/src/GenerationLifecycle.{hpp,cpp}`) is the right module: copies of the script assembly, loading, replacing, retiring and sweeping generations, the restore of the previous copy when a new build fails, the re-entrancy guard, behind `Load`, `Reload`, `Unload`, `Sweep`, `Release` and `Poll`. Its interface is deep. Its one dependency is not a seam: it holds a raw `Iris::Host*` and calls `LoadComponent`, `Release` and `WaitForUnload` on it, keeps `Iris::Component` owners for the active and retired generations, and its four `Hooks` take `const Iris::Component&`. So every rule it enforces is tested only in `Tests/ScriptingTests.cpp`, through .NET, a built fixture assembly and its "version 2", with the real runtime deciding when a generation is collected. The rules a reader most needs to trust (the previous copy restored when a build fails; a generation that starts and fails released again; a released generation swept only once collected; `Reload` refused while a reload runs) have no test that states them on their own.

What a generation registers, and what survives a reload, is spread over five files:

- `ScriptSchema.{hpp,cpp}`: `OwnedScriptData` (the layouts of the script components and events whose data is alive), `Reconcile` (discover, check, `DropStale`, `Keep` per kind), `DropScriptData`.
- `Scripting.cpp`: `State::owned`, `StopSystems` calling `components.RemoveScriptDefined()`, `Shutdown` calling `DropScriptData`, the `reconcile` and `stop` hooks.
- `ComponentRegistry.hpp`: `AddAll`, `AddEvents`, `RemoveScriptDefined`, called from two of the files above.
- `EngineContext.hpp`: `discovering` and `discoveringEvents`, two raw vector pointers set by `IrisScriptAssembly::Report` for the duration of one call.
- `Bindings.cpp`: the natives `RegisterScriptComponent` and `RegisterScriptEvent` that fill them.

And the keeping is not all or nothing. `Reconcile` runs `DropStale` for both kinds, then `Keep` for the components (which registers them through `AddAll`), then `Keep` for the events. If the events' Keep fails, `LoadGeneration` retires the Iris component and returns the failure without calling the `stop` hook, because the generation was never made active. The script components stay registered with no generation loaded. `Reload` then loads the previous copy again, whose admission fails its own first check ("the engine already has a component of that name"), so a failure in the events' Keep loses the running scripts as well as the new build. The failure is unlikely (`AddEvents` and `CreateEventQueue` can only fail for names the checks already passed), but the rule "nothing is half-registered" is one `Reconcile` claims in its comment and does not hold.

## Solution

One seam under the lifecycle, one module for script data, one slot instead of two, and the lifecycle's rules each stated as a Rules test with no .NET.

### `ComponentLoader`: the seam under `GenerationLifecycle`

```cpp
/** What a loaded generation is to the lifecycle: something to release and to wait for. */
class LoadedComponent
{
  public:
    virtual ~LoadedComponent() = default;
    [[nodiscard]] virtual Iris::CallResult<void> Release() = 0;
    [[nodiscard]] virtual Iris::CallResult<Iris::ComponentUnloadState> WaitForUnload(std::chrono::milliseconds) = 0;
};

/** Where a generation's copy is loaded from, and what comes back. */
class ComponentLoader
{
  public:
    virtual ~ComponentLoader() = default;
    [[nodiscard]] virtual Iris::CallResult<std::unique_ptr<LoadedComponent>> Load(const std::filesystem::path&) = 0;
};
```

`GenerationLifecycle::Configure` takes a `ComponentLoader&` in place of the `Iris::Host*`. The active generation and each retired one are `std::unique_ptr<LoadedComponent>`. The four `Hooks` keep their meaning and take `LoadedComponent&` in place of `const Iris::Component&`. The header keeps `Iris::CallResult` and `Iris::ComponentUnloadState` as value types and names nothing else of Iris.

Two adapters stand at the seam:

- **`IrisComponentLoader`**, private to `Scripting.cpp`, over the `Iris::Host`: its `LoadedComponent` holds the `Iris::Component`, and `static Iris::Component& Of(LoadedComponent&)` is the one cast, confined to the adapter that made the object. Scripting's hooks and `Scripting::LoadedComponent()` (the probe's hook) go through it.
- **`FakeComponentLoader`**, test code in `Tests/Rules/`, beside `LiteralAssembly`: it can be told to fail the Nth load or a given path, to report `Pending` for the first N waits of a token and `Collected` after, and to refuse a `Release` with `InUse`. It records the paths loaded in order and the tokens released, so a test proves the copy was loaded and not the original, and that a failed generation was released.

Copying stays real: `MakeCopy` is plain filesystem work that varies across nothing, so it earns no seam. The Rules tests give the lifecycle a scratch directory holding a small dummy "assembly" and let it copy, so the rule that a copy is deleted only once its generation is collected is tested against the real filesystem with the fake deciding when "collected" happens.

### `ScriptData`: one module for what a generation leaves behind

A class, in `ScriptSchema.{hpp,cpp}` renamed `ScriptData.{hpp,cpp}`, owning the two layout maps (today's `OwnedScriptData`) and the registry's script-defined entries. It is constructed with the `ComponentRegistry&` it manages, a `State` member declared after `components`, and takes the `App&` per call, because the App is attached in a later Install stage than the State is made. It is the only caller of `AddAll`, `AddEvents` and `RemoveScriptDefined`.

```cpp
class ScriptData
{
  public:
    explicit ScriptData(ComponentRegistry& registry);
    /** Admission: keeps what the assembly reports, all or nothing. A failure leaves the registry as Forget would and the data as the drop of stale layouts left it. */
    [[nodiscard]] Iris::CallResult<void> Admit(App& app, ScriptAssembly& assembly);
    /** The stop path: the registry's script-defined entries go; the data and its layouts stay for the next generation. */
    void Forget();
    /** Shutdown: the data, its queues, the layouts and the registrations all go. */
    void Drop(App& app);
};
```

`Admit` does what `Reconcile` does today, in a new order so the irreversible step comes as late as possible and everything after it can be undone:

1. Discover each kind from the script report and check it (unchanged rules and diagnostics).
2. Check every native component, event, shared struct and resource against its C# struct (unchanged).
3. Register the script components and the script events (reversible through `Forget`).
4. Drop stale data: component data and queues whose layout changed or whose definition is gone, with today's warnings (irreversible).
5. Make the queues of new events.

A failure after step 3 calls `Forget`, removes any queue this admission made, and restores the layouts to what survived step 4. A failed admission therefore leaves "no generation, the registry as after a stop, the data as the drop left it", which is exactly the state a restore of the previous copy admits against. The free functions `Reconcile` and `DropScriptData` go; `ScriptAssembly`, `IrisScriptAssembly` and `LiteralAssembly` stay as the assembly-side seam, and the Iris adapter is constructed in `Scripting.cpp`'s `reconcile` hook.

### One `ScriptReport` slot

`EngineContext::discovering` and `discoveringEvents` become one `ScriptReport* reporting`, where `ScriptReport` is a value of the two description vectors, the glossary's *script report* in code. `ScriptAssembly::Report(ScriptKind, found)` becomes `Report(ScriptReport&)`, one call that runs both generated tables; `IrisScriptAssembly` sets the slot for its duration and clears it after; the two natives in `Bindings.cpp` push into the report's vectors. The slot cannot leave `EngineContext`, because a native reaches nothing but its extension state.

### Out of scope

- Install's extension validation (review candidate 8): the checks are interleaved with registration in one stage, and it is not the generation lifecycle. It stays.
- The registry's three tables (review candidate 5): `ComponentRegistry.hpp`'s shape is unchanged; only its callers move.
- The .NET reload tests in `ScriptingTests.cpp`: they prove the real runtime unloads and a real build changes behaviour, which no fake can. They stay as they are.

## Decisions (from the grilling)

1. The seam is a three-method `ComponentLoader` returning an abstract `LoadedComponent` owner; the four hooks keep their meaning and take `LoadedComponent&`; the one cast to `Iris::Component` is the Iris adapter's own.
2. The fake loader has exactly these knobs: fail the Nth load or a path, `Pending` for N waits then `Collected`, refuse a release; it records loads and releases.
3. Copying stays real in the Rules tests, against a scratch directory.
4. `ScriptData` owns the layouts and the registry's script-defined entries, with `Admit(App&, ScriptAssembly&)`, `Forget()` and `Drop(App&)`; constructed with the registry, the App per call.
5. All-or-nothing by reorder and undo: register before the irreversible drop, make queues last, and a failure after registration forgets, removes the queues it made and restores the surviving layouts.
6. The two discovering pointers become one `ScriptReport*` slot and `Report` is one call; part of ticket 02.
7. Install's extension validation stays as it is.
8. Two tickets, stacked (both edit `Scripting.cpp`); the .NET reload tests are left alone.
9. Glossary: *script data* and *admission*, landing with ticket 02.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `ComponentLoader` and `LoadedComponent`, the Iris adapter, the fake, the lifecycle Rules tests | none |
| 02 | `ScriptData` with `Admit`/`Forget`/`Drop`, the reorder and undo, the `ScriptReport` slot, `ReconcileTests.cpp` rewritten onto `ScriptData`, glossary | 01 |

Ticket 02's branch is cut from ticket 01's and its PR targets it until 01 merges.

## Done looks like

- `GenerationLifecycle.hpp` names no `Iris::Host` and no `Iris::Component`; `grep -n "Iris::" packages/FenrirScripting/src/GenerationLifecycle.hpp` finds only `CallResult` and `ComponentUnloadState`.
- Each reload rule has a Rules test with no .NET: the copy is what loads; the previous copy is restored when the new build fails to load, to admit or to start; a start failure releases the new generation; a released generation is swept and its copy deleted only once collected, with one warning while it is pending; `Reload` from inside a hook is `NotInitialized`; `Release` leaves no copies and removes an empty scratch directory.
- Admission is all or nothing: a Rules test makes the events' keep fail after the components' succeeded and finds nothing script-defined in the registry, no queue made, the surviving layouts intact, and a second admission of the previous report succeeding.
- `AddAll`, `AddEvents` and `RemoveScriptDefined` are called from `ScriptData.cpp` only; `EngineContext` has one `reporting` slot.
- `GLOSSARY.md` defines *script data* and *admission*; the README's "Loading is all or nothing" and "Hot reload" sections say what the code does.
- The msvc-x64-debug preset builds and tests locally, `FenrirScripting` (the .NET tests) included; the other presets are CI's.
