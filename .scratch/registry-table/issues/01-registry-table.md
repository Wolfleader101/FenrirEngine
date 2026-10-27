# 01: `RegistryTable<Registration>`: one table behind the registry's four kinds

**What to build:** The one table implementation and the registry made of it (spec: "`RegistryTable<Registration>`"), with its callers moved onto it. A refactor: behaviour, messages and ordering unchanged, and the tests that pin them pass before and after.

- **`RegistryTable<Registration>`** in `src/ComponentRegistry.hpp`: constructed with a reference to the registry's generation (the `std::uint32_t` it bumps from the static counter), a reference to the registry's set of every name any table holds, and its noun. `Resolve(name, typeName)` returns the id, or `-1` (not registered) or `-3` (another C# type) as today; ticket 02 turns those into `RegistryError`. `Add`, `AddAll` (all or none; the `CanAddAll` rule, which now also consults the cross-kind set), `Find`, `Get`, `All`, `RemoveWhere(predicate)` (re-ids what remains, bumps the generation once), `Noun()`.
- **`ComponentRegistry`**: four tables, `Components()`, `Events()`, `SharedStructs()`, `Resources()` (const and non-const), `Generation()`, and `RemoveScriptDefined()` sweeping components and events through `RemoveWhere`. Every per-kind method goes: `Resolve`, `ResolveEvent`, `ResolveResource`, `Add`, `AddEvent`, `AddResource`, `AddSharedStruct`, `AddAll`, `AddEvents`, `Find`, `FindEvent`, `Get`, `GetEvent`, `GetResource`, `All`, `Events`, `Resources`, `SharedStructs` (the vector accessors become the table accessors).
- **Callers**: `src/Bindings.cpp`, `src/ChangeBuffer.cpp`, `src/Queries.cpp`, `src/ScriptData.cpp`, `src/Scripting.cpp`, `Tests/Rules/*.cpp`, `Tests/NativeCallHarness.hpp`, `Tests/ScriptingTests.cpp` (where it reaches the registry through the probe) write `registry.Events().Resolve(...)` and the like.
- **Install** (`src/Scripting.cpp`): the `std::unordered_set<std::string> names` goes; the four loops become one over the four tables, keeping the event `bind` step before the event table's `Add`, with the failure text `"a " + noun + " in " + source + " has an empty or duplicate name"` byte-identical to today's four strings.
- **ScriptData** (`src/ScriptData.cpp`): `ValidateAll`, `ValidateAllEvents`, `ValidateAllSharedStructs`, `ValidateAllResources` and `ValidateRegistration` become one template over a table, skipping an empty `validate`, with the diagnostic prefix `<Noun capitalised> '<name>' does not match its C# struct: ` as today.

**Blocked by:** none

**Touches:** `packages/FenrirScripting/src/ComponentRegistry.hpp`, `packages/FenrirScripting/src/Bindings.cpp`, `packages/FenrirScripting/src/ChangeBuffer.cpp`, `packages/FenrirScripting/src/Queries.cpp`, `packages/FenrirScripting/src/ScriptData.cpp`, `packages/FenrirScripting/src/Scripting.cpp`, `packages/FenrirScripting/Tests/`

**Status:** ready-for-agent

- [ ] Rules tests (no .NET), `Tests/Rules/RegistryTableTests.cpp`: `Add` gives ids in order and `Find` returns them; an empty name and a taken name are refused; a name another kind holds is refused; `AddAll` adds none when one is unusable, taken, or twice in the batch; `Resolve` gives the id for the registered type and `-3` for another; `RemoveWhere` re-ids what remains, `Find` follows, and the generation changes once; `Add` changes the generation and a lookup does not.
- [ ] `ScriptingTests` (.NET) pass unchanged: the duplicate-name startup failures, the "different C# type" refusals and the admission tests pin the messages.
- [ ] `grep -c "unordered_map<std::string, std::int32_t>" src/ComponentRegistry.hpp` is 1; `Scripting.cpp` declares no name set.
- [ ] `check_tickets.py`, clang-format and the msvc-x64-debug preset pass.

## Comments

Filed from the registry-table grilling (spec decisions 1 to 5, 12). Ticket 02 is cut from this branch.
