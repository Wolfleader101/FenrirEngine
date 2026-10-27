# 02: `RegistryError` and `ChangeError` described, generated, and returned by the built-in natives

**What to build:** The error codes as two described enums that reach C# through the generated pipeline, so each code has one C++ spelling and one generated C# spelling and C# switches on names (spec: "Two described enums", "What the built-in natives return").

- **`include/FenrirScripting/Errors.hpp`**, namespace `Fenrir`, Doxygen on each enumerator (the description carries the docs into the C# enum): `enum class RegistryError : std::int32_t { NotRegistered = -1, WrongType = -2 }` and `enum class ChangeError : std::int32_t { None = 0, EntityNotAlive = -1, Unknown = -2, ReadOnly = -3, Required = -4, NotActive = -5 }`. `ChangeError` leaves `src/ChangeBuffer.hpp`; its `UnknownComponent`/`UnknownEvent`/`UnknownResource` become `Unknown` and `ReadOnlyComponent`/`ReadOnlyResource` become `ReadOnly` in `ChangeBuffer.cpp` and its Rules tests.
- **`RegistryTable::Resolve`** returns `RegistryError` values cast to `std::int32_t` for its two failures (so an id native's result stays "id or error"); `Add` likewise returns `NotRegistered` for a refused name.
- **The description**: `api/FenrirScripting.api.toml` (headers `["../include/FenrirScripting/Errors.hpp"]`, both types with role `enum`), the committed `api/FenrirScripting.api.json`, `fenrir_describe_api(FenrirScripting api/FenrirScripting.api.toml)` in `packages/FenrirScripting/CMakeLists.txt`, and `iris_generate_binding(FenrirScripting api/FenrirScripting.binding.toml ../FenrirECS/api/FenrirECS.api.json api/FenrirScripting.api.json)`. The binding's `[cpp] includes` gains `FenrirScripting/Errors.hpp` for the generated `EnumMapping`s.
- **The natives** (`src/Bindings.cpp`): `EntityDestroy`, `ComponentAdd`, `ComponentRemove`, `EventSend`, `ResourceInsert`, `ResourceRemove` return `ChangeError` with `@notrunning Fenrir::ChangeError::NotActive`; their `@return` says "`None` when recorded, otherwise why it was refused" naming enumerators, no numbers. `ComponentIdOf`, `EventIdOf`, `ResourceIdOf` keep `std::int32_t`, documented as the id or a `RegistryError` value. `EventReadAfter` returns `bool`.
- **Regenerate**: `fenrir-describe-api` (the new description and `FenrirScripting.natives.json`) then `fenrir-generate-bindings` (`Generated/RegistryError.cs`, `Generated/ChangeError.cs`, `Generated/Native.cs`, `include/FenrirScripting/Generated/FenrirScriptingMappings.hpp`); both are no-ops afterwards.
- **C#**: `Entity.Check`, `Resources.Check` and `SystemContext.EventsView.Send` switch on `ChangeError` names with the messages they throw today; `Entity.IdOf`, `Resources.IdOf` and `EventIds<T>.Id` compare with `RegistryError` names; `ComponentIds`' and `ResourceIds`' `NotRegistered`/`WrongType` constants go. `EventReader<T>.Read` tests the `bool`.
- **ADR-0044**: one amendment line under Consequences: a native's result may be a described enum, which the binding generates, and FenrirScripting describes its own error enums this way.

**Blocked by:** 01

**Touches:** `packages/FenrirScripting/include/FenrirScripting/`, `packages/FenrirScripting/src/`, `packages/FenrirScripting/api/`, `packages/FenrirScripting/CMakeLists.txt`, `packages/FenrirScripting/Managed/Fenrir.Scripting/`, `packages/FenrirScripting/Tests/`, `docs/adr/0044-natives-pass-enums-text-and-wrappers-by-value-and-bindings-use-other-bindings-mirrors.md`

**Status:** resolved

- [ ] `ScriptingTests` (.NET): the thrown messages for not alive, no such component, read-only component, required component, scripting not running, unknown event, no such resource and read-only resource are each pinned by a case; add a case only where none exists today.
- [ ] Rules tests: `ChangeBufferTests.cpp` compares against the new enumerators; a `RegistryTableTests.cpp` case reads `Resolve`'s failures as `RegistryError`.
- [ ] `grep -n "=> \$\|== -\|code == -" Managed/Fenrir.Scripting/Entity.cs Resources.cs Events.cs` finds nothing; `Bindings.cpp` doc comments name no number.
- [ ] `fenrir-describe-api` and `fenrir-generate-bindings` are no-ops after the commit; `check_tickets.py`, clang-format and the msvc-x64-debug preset pass.

## Comments

Filed from the registry-table grilling (spec decisions 6 to 9, 11, 15). Cut from `registry-table-01-table`; ticket 03 is cut from this branch.
