# 02: `ScriptData` owns a generation's script data and admits a report all or nothing

**What to build:** One module for what a generation registers and what survives a reload, with admission that cannot be left half done (spec: "`ScriptData`: one module for what a generation leaves behind", "One `ScriptReport` slot").

- **`src/ScriptData.{hpp,cpp}`** (renamed from `ScriptSchema.{hpp,cpp}`): `class ScriptData` constructed with `ComponentRegistry&`, with `Admit(App&, ScriptAssembly&)`, `Forget()` and `Drop(App&)`, in the spec's order: discover and check, the native check, register components and events, drop stale data, make new queues; a failure after registration forgets, removes the queues this admission made and restores the layouts that survived the drop. `OwnedScriptData`, `Reconcile` and `DropScriptData` go; `OwnedLayout`, `ScriptAssembly`, `IrisScriptAssembly` and `ValidateRegistration` stay. `ScriptReport` (the two description vectors) is declared here; `ScriptAssembly::Report(ScriptReport&)` is one call. Diagnostics and warnings are unchanged.
- **`src/EngineContext.hpp`** and **`src/Bindings.cpp`**: `discovering` and `discoveringEvents` become `ScriptReport* reporting`; `RegisterScriptComponent` and `RegisterScriptEvent` push into it.
- **`src/Scripting.cpp`**: `State::owned` becomes `Detail::ScriptData scriptData` declared after `components`; the `reconcile` hook makes an `IrisScriptAssembly` and calls `Admit`; `StopSystems` calls `Forget()` in place of `components.RemoveScriptDefined()`; `Shutdown` calls `Drop`. No other file calls `AddAll`, `AddEvents` or `RemoveScriptDefined`.
- **`Tests/Rules/ScriptDataTests.cpp`** (renamed from `ReconcileTests.cpp`, replacing it): every case rewritten onto `ScriptData`'s interface (`Admit`, `Forget`, `Drop`), `LiteralAssembly` reporting through `Report(ScriptReport&)`, plus the all-or-nothing case: the events' keep made to fail after the components' succeeded. Find a report the checks let through and the keep refuses (an event with an empty name is the first candidate: the event check does not refuse it today, and `AddEvents` does); if none exists, the test says so and the check that closes the gap is added to `Admit` so the case is reachable through the interface. Record which in the test's comment.
- **`GLOSSARY.md`**: *script data* and *admission* under FenrirEngine scripting, in the spec's words, with *Avoid:* reconcile, schema. The *script report* entry notes the `ScriptReport` type.
- **`README.md`**: "Loading is all or nothing" says what a failed admission leaves; "Hot reload" and the Tests section name `ScriptData`.
- **`CMakeLists.txt`** (the package's source list) and `Tests/CMakeLists.txt`: the renamed files.

**Blocked by:** 01

**Touches:** `packages/FenrirScripting/src/ScriptData.hpp`, `packages/FenrirScripting/src/ScriptData.cpp`, `packages/FenrirScripting/src/ScriptSchema.hpp`, `packages/FenrirScripting/src/ScriptSchema.cpp`, `packages/FenrirScripting/src/EngineContext.hpp`, `packages/FenrirScripting/src/Bindings.cpp`, `packages/FenrirScripting/src/Scripting.cpp`, `packages/FenrirScripting/CMakeLists.txt`, `packages/FenrirScripting/Tests/Rules/`, `packages/FenrirScripting/Tests/CMakeLists.txt`, `packages/FenrirScripting/README.md`, `GLOSSARY.md`

**Status:** ready-for-agent

- [ ] Rules tests (no .NET): every former `ReconcileTests` case passes through `ScriptData`; a failed admission after the components were registered leaves nothing script-defined in the registry, no queue made, the surviving layouts intact, and a second admission of the previous report succeeding; `Forget` keeps the data and layouts and removes the registrations; `Drop` empties everything.
- [ ] `grep -rn "AddAll\|AddEvents\|RemoveScriptDefined" packages/FenrirScripting/src` finds the registry's declarations and `ScriptData.cpp` only.
- [ ] The .NET tests in `ScriptingTests.cpp` pass unchanged, the bad-report cases included.
- [ ] `check_tickets.py`, clang-format and the msvc-x64-debug preset pass.

## Comments

Filed from the generation lifecycle grilling (spec decisions 4, 5, 6, 9). Stacks on ticket 01.
