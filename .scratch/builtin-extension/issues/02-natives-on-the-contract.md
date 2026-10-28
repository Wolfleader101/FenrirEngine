# 02: The built-in natives on the generated contract

**What to build:** every native in `src/Bindings.cpp` follows ADR-0045's contract and ADR-0044's text kind, as every package native does, so the one natives file the README points at shows one convention (spec: "The built-in natives on the contract").

- **`src/Bindings.cpp`**:
  - `Log(const CallContext&, Detail::EngineContext&, std::int32_t level, std::string_view text)`: no null or length check; `std::string(text)` where it formats.
  - `ComponentIdOf`, `EventIdOf`, `ResourceIdOf`: `(..., std::string_view name, std::string_view typeName)`, each one line calling its table's `Resolve`.
  - `RegisterScriptComponent`, `RegisterScriptEvent`: `(..., std::string_view name, std::int32_t size, std::int32_t alignment)`; the rule stays as `engine.reporting == nullptr || name.empty() || size <= 0 || alignment <= 0`.
  - `EntityWorldTransform`: no `world == nullptr` check and no `*world = Transform{}`; its doc's `@param[out]` line keeps saying the default is what C# reads when it returns false, because the registration's `ZeroingOutputs<..., 3>` makes it so.
  - `EntitySetWorldTransform`: no `world == nullptr` check.
  - `EventReadAfter`: no `seen == nullptr || view == nullptr` check; the queue-size rule stays.
  - `QueryCount`, `QueryFill`: `const auto ids = SpanOf(idsFirst, count)` (and `rows` likewise); `if (ids.empty()) return -1;` stays as the rule that a query of nothing is refused, so `QueryNative.cs` still throws for it. Include `FenrirScripting/NativeMarshalling.hpp`.
  - The namespace's header comment: the declaration is the contract (ADR-0045: an `in` or `out` pointer is never null and a single output is zeroed by the registration), text arrives as `std::string_view` (ADR-0044), and a code a native returns is `ChangeBuffer`'s or the registry's rule.
  - The nine code-returning natives (`EntityDestroy`, `ComponentAdd`, `ComponentRemove`, `EventSend`, `ResourceInsert`, `ResourceRemove`, and the three `*IdOf` apart from their text) are unchanged beyond the above.
- **Regenerate**: `fenrir-describe-api` (natives mode) rewrites `api/FenrirScripting.natives.json` with `"kind": "string"` where the `i8` pairs were and `span`s for the query arrays; `fenrir-generate-bindings` rewrites `src/Generated/FenrirScriptingNatives.hpp` and `Managed/Fenrir.Scripting/Generated/Native.cs` (`Log(int level, ReadOnlySpan<char> text)`, `ComponentIdOf(ReadOnlySpan<char> name, ReadOnlySpan<char> typeName)`, and so on). The C# callers (`Log.cs`, `RegistryIds.cs`, `ScriptComponents.cs`, `Events.cs`) pass `string`s, which convert; none changes.
- **`docs/adr/0045-...md`**: already carries the amendment line (landed with the spec); nothing to edit unless the wording proves wrong in the doing.
- No `NativeGuard`, no `WarnOnceRecord` on `EngineContext` (spec decision 6): C# throws on these failures already.

**Blocked by:** none

**Touches:** `packages/FenrirScripting/src/Bindings.cpp`, `packages/FenrirScripting/api/FenrirScripting.natives.json`, `packages/FenrirScripting/src/Generated/FenrirScriptingNatives.hpp`, `packages/FenrirScripting/Managed/Fenrir.Scripting/Generated/Native.cs`

**Status:** resolved
- [x] `grep -n "const char\*" packages/FenrirScripting/src/Bindings.cpp` finds nothing; `grep -n "== nullptr" packages/FenrirScripting/src/Bindings.cpp` finds no parameter of a native (registry and queue lookups may still compare their results); `grep -c '"i8"' packages/FenrirScripting/api/FenrirScripting.natives.json` is 0.
- [x] `Native.cs` declares `Log(int level, ReadOnlySpan<char> text)` and `QueryCount(ReadOnlySpan<int> ids)`; the contract and every fixture build without a change outside `Generated/`.
- [x] The `ScriptingTests.cpp` cases for `Log`, `WorldTransform`, queries and event readers pass unchanged; every scripting test passes under the msvc-x64-debug preset; `fenrir-describe-api` and `fenrir-generate-bindings` are no-ops after the commit; `check_tickets.py` and clang-format pass.

## Comments

Filed from the built-in-extension grilling (spec decisions 6, 7, 8). Cut from `dotnet-scripting`, beside 01, with which it shares no file. 03 is cut from this ticket's branch.
